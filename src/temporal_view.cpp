#include "temporal_view.hpp"
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include <algorithm>
#include <cmath>

namespace forge {
using namespace Diligent;
bool TemporalView::discontinuous(const CameraView& old, const CameraView& current) {
    double forward = 0, up = 0;
    for (unsigned axis = 0; axis < 3; ++axis) {
        forward += old.forward[axis] * current.forward[axis];
        up += old.up[axis] * current.up[axis];
    }
    return old.projection != current.projection || old.viewport != current.viewport ||
           old.orientation_reversed != current.orientation_reversed || forward < .5 || up < .5 ||
           std::hypot(current.position[0] - old.position[0], current.position[1] - old.position[1],
                      current.position[2] - old.position[2]) > 10;
}
TemporalView::TemporalView(DiligentPresentation& presentation, std::uint64_t byte_limit)
    : presentation_(presentation), byte_limit_(byte_limit), filter_(presentation, byte_limit) {
    if (byte_limit < bytes_per_pixel)
        throw std::invalid_argument("Temporal view budget cannot hold one pixel");
    ShaderCreateInfo ci;
    ci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    ci.EntryPoint = "main";
    ci.Desc.Name = "FORGE temporal rectangle VS";
    ci.Desc.ShaderType = SHADER_TYPE_VERTEX;
    ci.Source = R"(float4 main(uint id:SV_VertexID):SV_Position {
        return float4(id==2?3:-1,id==1?3:-1,0,1);
    })";
    RefCntAutoPtr<IShader> vs, ps;
    presentation_.shader(ci, &vs);
    ci.Desc.Name = "FORGE temporal color and depth composition PS";
    ci.Desc.ShaderType = SHADER_TYPE_PIXEL;
    ci.Source = R"(Texture2D<float4> g_Color; Texture2D<float> g_Depth;
    cbuffer ForgeTemporalRectangle { float4 rectangle; };
    struct Result {float4 color:SV_Target0; float depth:SV_Depth;};
    Result main(float4 p:SV_Position) {
        int2 pixel=int2(p.xy)-int2(rectangle.xy);
        Result result; result.color=g_Color.Load(int3(pixel,0));
        result.depth=g_Depth.Load(int3(pixel,0)); return result;
    })";
    presentation_.shader(ci, &ps);
    BufferDesc buffer;
    buffer.Name = "FORGE temporal destination rectangle";
    buffer.Size = 16;
    buffer.BindFlags = BIND_UNIFORM_BUFFER;
    buffer.Usage = USAGE_DYNAMIC;
    buffer.CPUAccessFlags = CPU_ACCESS_WRITE;
    presentation_.device()->CreateBuffer(buffer, nullptr, &rectangle_);
    if (!rectangle_)
        throw std::runtime_error("Temporal rectangle allocation failed");
    GraphicsPipelineStateCreateInfo info;
    info.PSODesc.Name = "FORGE temporal rectangle composition";
    info.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    const ShaderResourceVariableDesc inputs[]{
        {SHADER_TYPE_PIXEL, "g_Color", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
        {SHADER_TYPE_PIXEL, "g_Depth", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC}};
    info.PSODesc.ResourceLayout.Variables = inputs;
    info.PSODesc.ResourceLayout.NumVariables = Uint32(std::size(inputs));
    auto& g = info.GraphicsPipeline;
    g.NumRenderTargets = 1;
    g.RTVFormats[0] = TEX_FORMAT_RGBA16_FLOAT;
    g.DSVFormat = TEX_FORMAT_D32_FLOAT;
    g.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    g.RasterizerDesc.CullMode = CULL_MODE_NONE;
    g.DepthStencilDesc.DepthEnable = true;
    g.DepthStencilDesc.DepthWriteEnable = true;
    g.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_ALWAYS;
    info.pVS = vs;
    info.pPS = ps;
    presentation_.graphics(info, &composite_);
    composite_->CreateShaderResourceBinding(&binding_, true);
    if (!binding_)
        throw std::runtime_error("Temporal rectangle binding is unavailable");
    binding_->GetVariableByName(SHADER_TYPE_PIXEL, "ForgeTemporalRectangle")->Set(rectangle_);
}
void TemporalView::invalidate() {
    valid_ = prepared_ = begun_ = resolved_ = false;
    samples_ = 0;
    filter_.invalidate();
    destination_.Release();
    destination_depth_.Release();
    previous_parts_.clear();
    pending_parts_.clear();
}
void TemporalView::prepare(const CameraView& view, bool reset) {
    const auto& v = view.viewport;
    const auto maximum = presentation_.device()->GetAdapterInfo().Texture.MaxTexture2DDimension;
    if (!v.width || !v.height || v.width > maximum || v.height > maximum ||
        std::uint64_t(v.width) * v.height > byte_limit_ / bytes_per_pixel ||
        !std::all_of(view.projection.begin(), view.projection.end(),
                     [](float x) { return std::isfinite(x); }))
        throw std::invalid_argument("Temporal view extent or projection exceeds its profile");
    if (begun_)
        throw std::logic_error("Temporal view has an unfinished frame");
    if (!color_ || color_->GetDesc().Width != v.width || color_->GetDesc().Height != v.height) {
        RefCntAutoPtr<ITexture> color, motion, depth;
        TextureDesc desc;
        desc.Name = "FORGE temporal view HDR";
        desc.Type = RESOURCE_DIM_TEX_2D;
        desc.Width = v.width;
        desc.Height = v.height;
        desc.Format = TEX_FORMAT_RGBA16_FLOAT;
        desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
        presentation_.device()->CreateTexture(desc, nullptr, &color);
        desc.Name = "FORGE temporal surface motion";
        desc.Format = TEX_FORMAT_RGBA32_FLOAT;
        presentation_.device()->CreateTexture(desc, nullptr, &motion);
        desc.Name = "FORGE temporal current depth";
        desc.Format = TEX_FORMAT_D32_FLOAT;
        desc.BindFlags = BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE;
        presentation_.device()->CreateTexture(desc, nullptr, &depth);
        if (!color || !motion || !depth)
            throw std::runtime_error("Temporal view allocation failed; previous targets retained");
        color_ = std::move(color);
        motion_ = std::move(motion);
        depth_ = std::move(depth);
        reset = true;
    }
    if (reset)
        invalidate();
    pending_parts_.clear();
    area_ = v;
    current_ = view;
    current_.viewport = {0, 0, v.width, v.height};
    jitter_ = TemporalFilter::jitter(samples_, v.width, v.height);
    // Row-major projection, column vectors: add an NDC offset times clip W.
    for (unsigned c = 0; c < 4; ++c) {
        current_.projection[c] += jitter_[0] * view.projection[12 + c];
        current_.projection[4 + c] += jitter_[1] * view.projection[12 + c];
    }
    resolved_ = false;
    prepared_ = true;
}
void TemporalView::begin(IDeviceContext* context, ITexture* color, ITextureView* depth) {
    if (!context || !color || !depth || !color_ || !prepared_ || begun_ ||
        color->GetDesc().Format != TEX_FORMAT_RGBA16_FLOAT ||
        depth->GetDesc().ViewType != TEXTURE_VIEW_DEPTH_STENCIL ||
        depth->GetTexture()->GetDesc().Format != TEX_FORMAT_D32_FLOAT || color == color_.RawPtr() ||
        area_.x >= color->GetDesc().Width || area_.y >= color->GetDesc().Height ||
        area_.width > color->GetDesc().Width - area_.x ||
        area_.height > color->GetDesc().Height - area_.y ||
        color->GetDesc().Width != depth->GetTexture()->GetDesc().Width ||
        color->GetDesc().Height != depth->GetTexture()->GetDesc().Height)
        throw std::invalid_argument("Temporal view requires matching composed HDR/depth targets");
    const auto complete_target = [](const TextureDesc& desc, BIND_FLAGS binding) {
        return desc.Type == RESOURCE_DIM_TEX_2D && desc.ArraySize == 1 && desc.MipLevels == 1 &&
               desc.SampleCount == 1 && (desc.BindFlags & binding) != 0;
    };
    const auto& dsv_desc = depth->GetDesc();
    if (!complete_target(color->GetDesc(), BIND_RENDER_TARGET) ||
        !complete_target(depth->GetTexture()->GetDesc(), BIND_DEPTH_STENCIL) ||
        dsv_desc.TextureDim != RESOURCE_DIM_TEX_2D || dsv_desc.MostDetailedMip != 0 ||
        dsv_desc.FirstArraySlice != 0 || dsv_desc.NumArraySlices != 1)
        throw std::invalid_argument("Temporal composition needs complete single-sample 2D targets");
    destination_ = color;
    destination_depth_ = depth;
    context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
    Box box{area_.x, area_.x + area_.width, area_.y, area_.y + area_.height};
    CopyTextureAttribs copy{color, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, color_,
                            RESOURCE_STATE_TRANSITION_MODE_TRANSITION};
    copy.pSrcBox = &box;
    context->CopyTexture(copy); // Color rectangle only; depth subrectangle copies are unnecessary.
    auto* dsv = depth_->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
    auto* motion = motion_->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
    const float zero[4]{};
    context->ClearRenderTarget(motion, zero, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                               RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    ITextureView* targets[]{color_->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET), motion};
    context->SetRenderTargets(2, targets, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    Diligent::Viewport viewport{0, 0, float(area_.width), float(area_.height), 0, 1};
    context->SetViewports(1, &viewport, area_.width, area_.height);
    begun_ = true;
    prepared_ = false;
}
void TemporalView::resolve(IDeviceContext* context, float stability, bool complete_geometry) {
    if (!begun_ || resolved_)
        throw std::logic_error("Temporal resolve requires one begun, unresolved view");
    const auto& projection = previous().projection;
    TemporalFilter::Input input;
    input.color = color_->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    input.motion = motion_->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    input.depth = depth_->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    input.previous_depth_projection = {projection[10], projection[11], projection[14],
                                       projection[15]};
    input.jitter = jitter_;
    input.previous_jitter = previous_jitter();
    input.stability = stability;
    input.reset = !valid_ || !complete_geometry;
    auto* result = filter_.resolve(context, input);
    {
        MapHelper<float> data(context, rectangle_, MAP_WRITE, MAP_FLAG_DISCARD);
        data[0] = float(area_.x);
        data[1] = float(area_.y);
        data[2] = data[3] = 0;
    }
    auto* color = binding_->GetVariableByName(SHADER_TYPE_PIXEL, "g_Color");
    auto* depth = binding_->GetVariableByName(SHADER_TYPE_PIXEL, "g_Depth");
    color->Set(result);
    depth->Set(input.depth);
    auto* rtv = destination_->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
    context->SetRenderTargets(1, &rtv, destination_depth_,
                              RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    Diligent::Viewport viewport{
        float(area_.x), float(area_.y), float(area_.width), float(area_.height), 0, 1};
    context->SetViewports(1, &viewport, destination_->GetDesc().Width,
                          destination_->GetDesc().Height);
    context->SetPipelineState(composite_);
    context->CommitShaderResources(binding_, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->Draw(DrawAttribs{3, DRAW_FLAG_VERIFY_ALL});
    color->Set(nullptr);
    depth->Set(nullptr);
    resolved_ = true;
}
void TemporalView::complete() {
    if (!begun_ || !resolved_)
        throw std::logic_error("Temporal history cannot adopt an incomplete frame");
    previous_ = current_;
    previous_jitter_ = jitter_;
    previous_parts_.swap(pending_parts_);
    // Keep the phase modulo eight and a bounded warm-up count without overflow.
    samples_ = samples_ < 16 ? samples_ + 1 : 9;
    valid_ = true;
    begun_ = resolved_ = false;
    destination_.Release();
    destination_depth_.Release();
}
} // namespace forge
