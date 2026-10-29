#include "temporal_filter.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace forge {
using namespace Diligent;
namespace {
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
constexpr char vertex[] = R"hlsl(
float4 main(uint vertex:SV_VertexID):SV_POSITION {
    float2 position=vertex==0?float2(-1,-1):vertex==1?float2(-1,3):float2(3,-1);
    return float4(position,0,1);
}
)hlsl";
constexpr char pixel[] = R"hlsl(
Texture2D<float4> Source;
Texture2D<float4> Motion;
Texture2D<float4> History;
Texture2D<float> OldDepth;
cbuffer TemporalValues {float4 Extent;float4 DepthSample;float4 DepthProjection;};
float viewDepth(float depth) {
    float denominator=depth*DepthProjection.z-DepthProjection.x;
    if(abs(denominator)<1e-20)return -1;
    float result=(DepthProjection.y-depth*DepthProjection.w)/denominator;
    return isfinite(result)&&result>0?result:-1;
}
float4 main(float4 position:SV_POSITION):SV_Target {
    int2 p=int2(position.xy),size=int2(Extent.xy);
    float4 surface=Source.Load(int3(p,0));
    float3 current=surface.rgb;
    if(Extent.w!=0)return surface;
    float4 movement=Motion.Load(int3(p,0));
    float2 oldPixel=position.xy-float2(.5,-.5)*movement.xy*Extent.xy;
    if(movement.w==0||!all(isfinite(movement))||any(oldPixel<.5)||
       any(oldPixel>Extent.xy-.5))return surface;

    // Correspondence depth uses the previous raster's sampling footprint. Color
    // history stays on its accumulation grid, independent of projection jitter.
    float expected=viewDepth(movement.z);
    if(expected<=0)return surface;
    int2 baseDepth=int2(floor(oldPixel+DepthSample.xy-.5));
    float error=1e30;
    for(int y=0;y<2;++y)for(int x=0;x<2;++x) {
        int2 at=baseDepth+int2(x,y);
        if(all(at>=0)&&all(at<size)) {
            float prior=viewDepth(OldDepth.Load(int3(at,0)));
            if(prior>0)error=min(error,abs(prior-expected));
        }
    }
    if(error>max(.002,.02*expected))return surface;

    float3 low=current,high=current;
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x) {
        float3 neighbor=Source.Load(int3(clamp(p+int2(x,y),0,size-1),0)).rgb;
        low=min(low,neighbor);high=max(high,neighbor);
    }
    float2 historyPosition=oldPixel-.5;
    int2 base=int2(floor(historyPosition));float2 fraction=frac(historyPosition);
    float3 h00=History.Load(int3(clamp(base,0,size-1),0)).rgb;
    float3 h10=History.Load(int3(clamp(base+int2(1,0),0,size-1),0)).rgb;
    float3 h01=History.Load(int3(clamp(base+int2(0,1),0,size-1),0)).rgb;
    float3 h11=History.Load(int3(clamp(base+int2(1,1),0,size-1),0)).rgb;
    float3 prior=lerp(lerp(h00,h10,fraction.x),lerp(h01,h11,fraction.x),fraction.y);
    return float4(lerp(current,clamp(prior,low,high),Extent.z),surface.a);
}
)hlsl";
} // namespace

TemporalFilter::TemporalFilter(DiligentPresentation& presentation, std::uint64_t byte_limit)
    : presentation_(presentation), byte_limit_(byte_limit) {
    require(byte_limit >= 20, "Temporal history budget is too small");
    ShaderCreateInfo shader;
    shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    shader.EntryPoint = "main";
    shader.Desc.Name = "FORGE temporal screen vertex";
    shader.Desc.ShaderType = SHADER_TYPE_VERTEX;
    shader.Source = vertex;
    RefCntAutoPtr<IShader> vs, ps;
    presentation_.shader(shader, &vs);
    shader.Desc.Name = "FORGE temporal accumulation pixel";
    shader.Desc.ShaderType = SHADER_TYPE_PIXEL;
    shader.Source = pixel;
    presentation_.shader(shader, &ps);
    require(vs && ps, "Temporal shader compilation failed");
    GraphicsPipelineStateCreateInfo ci;
    ci.PSODesc.Name = "FORGE temporal accumulation";
    ci.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    const ShaderResourceVariableDesc inputs[]{
        {SHADER_TYPE_PIXEL, "Source", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
        {SHADER_TYPE_PIXEL, "Motion", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
        {SHADER_TYPE_PIXEL, "History", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
        {SHADER_TYPE_PIXEL, "OldDepth", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC}};
    ci.PSODesc.ResourceLayout.Variables = inputs;
    ci.PSODesc.ResourceLayout.NumVariables = Uint32(std::size(inputs));
    ci.GraphicsPipeline.NumRenderTargets = 1;
    ci.GraphicsPipeline.RTVFormats[0] = TEX_FORMAT_RGBA16_FLOAT;
    ci.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    ci.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_NONE;
    ci.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;
    ci.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = false;
    ci.pVS = vs;
    ci.pPS = ps;
    presentation_.graphics(ci, &pipeline_);
    require(bool(pipeline_), "Temporal pipeline creation failed");
    pipeline_->CreateShaderResourceBinding(&binding_, true);
    require(bool(binding_), "Temporal binding allocation failed");
    BufferDesc desc;
    desc.Name = "FORGE temporal accumulation values";
    desc.Size = 3 * sizeof(std::array<float, 4>);
    desc.Usage = USAGE_DEFAULT;
    desc.BindFlags = BIND_UNIFORM_BUFFER;
    presentation_.device()->CreateBuffer(desc, nullptr, &values_);
    require(bool(values_), "Temporal uniform allocation failed");
    auto* value = binding_->GetVariableByName(SHADER_TYPE_PIXEL, "TemporalValues");
    require(value != nullptr, "Temporal uniform binding is unavailable");
    value->Set(values_);
}

std::array<float, 2> TemporalFilter::jitter(std::uint64_t frame, unsigned width, unsigned height) {
    require(width && height, "Temporal jitter requires a nonempty view");
    constexpr int pattern[8][2] = {{-7, -1}, {-5, 7}, {-3, -5}, {-1, 3},
                                   {1, -7},  {3, 1},  {5, -3},  {7, 5}};
    return {float(double(pattern[frame % 8][0]) / (8.0 * width)),
            float(double(pattern[frame % 8][1]) / (8.0 * height))};
}

void TemporalFilter::prepare(IDeviceContext* context, unsigned width, unsigned height) {
    if (width_ == width && height_ == height)
        return;
    const auto maximum = presentation_.device()->GetAdapterInfo().Texture.MaxTexture2DDimension;
    require(width && height && width <= maximum && height <= maximum &&
                std::uint64_t(width) * height <= byte_limit_ / 20,
            "Temporal view exceeds its target or history budget");
    TextureDesc desc;
    desc.Name = "FORGE temporal color history";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = width;
    desc.Height = height;
    desc.Format = TEX_FORMAT_RGBA16_FLOAT;
    desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
    std::array<RefCntAutoPtr<ITexture>, 2> history;
    for (auto& image : history) {
        presentation_.device()->CreateTexture(desc, nullptr, &image);
        require(bool(image), "Temporal history allocation failed; previous targets retained");
    }
    desc.Name = "FORGE temporal depth history";
    desc.Format = TEX_FORMAT_D32_FLOAT;
    desc.BindFlags = BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE;
    RefCntAutoPtr<ITexture> depth;
    presentation_.device()->CreateTexture(desc, nullptr, &depth);
    require(bool(depth), "Temporal depth allocation failed; previous targets retained");
    const float clear[]{0, 0, 0, 1};
    for (auto& image : history)
        context->ClearRenderTarget(image->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET), clear,
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->ClearDepthStencil(depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL), CLEAR_DEPTH_FLAG,
                               1, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    history_ = std::move(history);
    depth_ = std::move(depth);
    width_ = width;
    height_ = height;
    last_ = 0;
    valid_ = false;
}

ITextureView* TemporalFilter::resolve(IDeviceContext* context, const Input& input) {
    require(context && input.color && input.motion && input.depth,
            "Temporal accumulation requires complete current inputs");
    for (const auto* view : {input.color, input.motion, input.depth}) {
        const auto& desc = view->GetDesc();
        require(desc.ViewType == TEXTURE_VIEW_SHADER_RESOURCE &&
                    desc.TextureDim == RESOURCE_DIM_TEX_2D && desc.MostDetailedMip == 0 &&
                    desc.NumMipLevels == 1 && desc.FirstArraySlice == 0 && desc.NumArraySlices == 1,
                "Temporal inputs require complete base-level 2D shader views");
    }
    require(std::isfinite(input.stability) && input.stability >= 0 && input.stability <= .95f,
            "Temporal stability must be finite in [0,0.95]");
    for (const auto value : input.previous_depth_projection)
        require(std::isfinite(value), "Temporal previous projection must be finite");
    const auto& projection = input.previous_depth_projection;
    require(double(projection[0]) * projection[3] != double(projection[1]) * projection[2],
            "Temporal previous depth projection is degenerate");
    for (unsigned axis = 0; axis < 2; ++axis)
        require(std::isfinite(input.jitter[axis]) && std::isfinite(input.previous_jitter[axis]),
                "Temporal projection jitter must be finite");
    const auto& color = input.color->GetTexture()->GetDesc();
    const auto shape = [&](const TextureDesc& value, TEXTURE_FORMAT format) {
        return value.Type == RESOURCE_DIM_TEX_2D && value.Width == color.Width &&
               value.Height == color.Height && value.SampleCount == 1 && value.Format == format;
    };
    require(shape(color, TEX_FORMAT_RGBA16_FLOAT) &&
                shape(input.motion->GetTexture()->GetDesc(), TEX_FORMAT_RGBA32_FLOAT) &&
                shape(input.depth->GetTexture()->GetDesc(), TEX_FORMAT_D32_FLOAT),
            "Temporal inputs require matching single-sample HDR, motion and depth targets");
    prepare(context, color.Width, color.Height);
    const unsigned next = valid_ ? 1 - last_ : 0;
    require(input.color->GetTexture() != history_[next] &&
                input.motion->GetTexture() != history_[next] && input.depth->GetTexture() != depth_,
            "Temporal input aliases a written history target");
    const std::array<std::array<float, 4>, 3> values{
        {{float(width_), float(height_), input.stability, float(input.reset || !valid_)},
         {(input.previous_jitter[0] - input.jitter[0]) * width_ * .5f,
          (input.jitter[1] - input.previous_jitter[1]) * height_ * .5f, 0, 0},
         projection}};
    context->UpdateBuffer(values_, 0, sizeof(values), values.data(),
                          RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    const auto bind = [&](const char* name, ITextureView* view) {
        auto* variable = binding_->GetVariableByName(SHADER_TYPE_PIXEL, name);
        require(variable != nullptr, "Temporal texture binding is unavailable");
        variable->Set(view);
    };
    bind("Source", input.color);
    bind("Motion", input.motion);
    bind("History", history_[1 - next]->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
    bind("OldDepth", depth_->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
    auto* target = history_[next]->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
    context->SetRenderTargets(1, &target, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    Viewport viewport{0, 0, float(width_), float(height_), 0, 1};
    context->SetViewports(1, &viewport, width_, height_);
    context->SetPipelineState(pipeline_);
    context->CommitShaderResources(binding_, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    DrawAttribs draw;
    draw.NumVertices = 3;
    draw.Flags = DRAW_FLAG_VERIFY_ALL;
    context->Draw(draw);
    context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
    CopyTextureAttribs copy;
    copy.pSrcTexture = input.depth->GetTexture();
    copy.pDstTexture = depth_;
    copy.SrcTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    copy.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    context->CopyTexture(copy);
    last_ = next;
    valid_ = true;
    return history_[last_]->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
}

std::uint64_t TemporalFilter::history_bytes() const { return std::uint64_t(width_) * height_ * 20; }
} // namespace forge
