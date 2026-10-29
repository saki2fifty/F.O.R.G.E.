#include "depth_of_field.hpp"
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"

namespace forge {
using namespace Diligent;
DepthOfField::DepthOfField(DiligentPresentation& presentation, std::uint64_t byte_limit)
    : presentation_(presentation), limit_(byte_limit) {
    if (byte_limit < 16 || byte_limit > depth_of_field_payload_limit)
        throw std::invalid_argument("Depth of field scratch budget must be 16 bytes to 128 MiB");
}
void DepthOfField::pipeline() {
    if (pipeline_)
        return;
    RefCntAutoPtr<IShader> vertex, pixel;
    ShaderCreateInfo shader;
    shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    shader.EntryPoint = "main";
    shader.Desc.ShaderType = SHADER_TYPE_VERTEX;
    shader.Desc.Name = "FORGE focus blur triangle";
    shader.Source = R"(float4 main(uint id:SV_VertexID):SV_Position {
        return float4(id==2?3:-1,id==1?3:-1,0,1);
    })";
    presentation_.shader(shader, &vertex);
    shader.Desc.ShaderType = SHADER_TYPE_PIXEL;
    shader.Desc.Name = "FORGE depth-aware focus blur";
    shader.Source = R"(
Texture2D<float4> FocusColor;
Texture2D<float> FocusDepth;
cbuffer FocusValues { float4 Rectangle; float4 Projection; float4 Focus; };
float distanceAt(float depth) {
    float denominator=depth*Projection.z-Projection.x;
    if(abs(denominator)<=1e-20)return -1;
    float result=(Projection.y-depth*Projection.w)/denominator;
    return isfinite(result)&&result>0?result:-1;
}
float4 main(float4 position:SV_Position):SV_Target {
    int2 pixel=int2(position.xy);
    float4 original=FocusColor.Load(int3(pixel,0));
    float depth=FocusDepth.Load(int3(pixel+int2(Rectangle.xy),0));
    float distance=distanceAt(depth);
    if (!(depth>=0 && depth<1 && distance>0)) return original;
    float radius=Focus.z*saturate(abs(distance-Focus.x)/Focus.y);
    if (radius<.5) return original;
    float3 sum=original.rgb;
    float weight=1;
    // Fixed disk:28 neighbours plus the center. Samples never cross the view
    // edge, depth discontinuity or focus plane. No fabricated translucent depth.
    [unroll] for(int y=-3;y<=3;++y) [unroll] for(int x=-3;x<=3;++x) {
        if ((x==0 && y==0) || x*x+y*y>9) continue;
        int2 at=pixel+int2(round(float2(x,y)*radius/3));
        if (any(at<0) || any(at>=int2(Rectangle.zw))) continue;
        float otherDepth=FocusDepth.Load(int3(at+int2(Rectangle.xy),0));
        float other=distanceAt(otherDepth);
        if (!(otherDepth>=0 && otherDepth<1 && other>0)) continue;
        if ((other-Focus.x)*(distance-Focus.x)<0) continue;
        if (abs(other-distance)>max(.01,.02*distance)) continue;
        sum+=FocusColor.Load(int3(at,0)).rgb; weight+=1;
    }
    return float4(sum/weight,original.a);
})";
    presentation_.shader(shader, &pixel);
    BufferDesc desc;
    desc.Name = "FORGE focus blur parameters";
    desc.Size = 48;
    desc.BindFlags = BIND_UNIFORM_BUFFER;
    desc.Usage = USAGE_DYNAMIC;
    desc.CPUAccessFlags = CPU_ACCESS_WRITE;
    RefCntAutoPtr<IBuffer> values;
    presentation_.device()->CreateBuffer(desc, nullptr, &values);
    if (!values)
        throw std::runtime_error("Depth of field parameter allocation failed");
    GraphicsPipelineStateCreateInfo info;
    info.PSODesc.Name = "FORGE spatial focus blur";
    info.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    const ShaderResourceVariableDesc inputs[]{
        {SHADER_TYPE_PIXEL, "FocusColor", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
        {SHADER_TYPE_PIXEL, "FocusDepth", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC}};
    info.PSODesc.ResourceLayout.Variables = inputs;
    info.PSODesc.ResourceLayout.NumVariables = Uint32(std::size(inputs));
    auto& graphics = info.GraphicsPipeline;
    graphics.NumRenderTargets = 1;
    graphics.RTVFormats[0] = TEX_FORMAT_RGBA16_FLOAT;
    graphics.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    graphics.RasterizerDesc.CullMode = CULL_MODE_NONE;
    graphics.DepthStencilDesc.DepthEnable = false;
    graphics.DepthStencilDesc.DepthWriteEnable = false;
    info.pVS = vertex;
    info.pPS = pixel;
    RefCntAutoPtr<IPipelineState> pipeline;
    RefCntAutoPtr<IShaderResourceBinding> binding;
    presentation_.graphics(info, &pipeline);
    if (!pipeline)
        throw std::runtime_error("Depth of field pipeline is unavailable");
    pipeline->CreateShaderResourceBinding(&binding, true);
    if (!binding || !binding->GetVariableByName(SHADER_TYPE_PIXEL, "FocusColor") ||
        !binding->GetVariableByName(SHADER_TYPE_PIXEL, "FocusDepth") ||
        !binding->GetVariableByName(SHADER_TYPE_PIXEL, "FocusValues"))
        throw std::runtime_error("Depth of field shader bindings are incomplete");
    binding->GetVariableByName(SHADER_TYPE_PIXEL, "FocusValues")->Set(values);
    pipeline_ = std::move(pipeline);
    binding_ = std::move(binding);
    values_ = std::move(values);
}
void DepthOfField::prepare(ITexture* color, ITextureView* depth, const CameraView& view,
                           const SceneDepthOfField& settings) {
    validate_scene_depth_of_field(settings);
    if (thread_ != std::this_thread::get_id())
        throw std::logic_error("Depth of field requires its presentation thread");
    if (!settings.enabled || settings.blur_radius == 0)
        return;
    if (!color || !depth)
        throw std::invalid_argument("Depth of field requires HDR color and sampled depth");
    const auto& source = color->GetDesc();
    const auto& d = depth->GetTexture()->GetDesc();
    const auto& srv = depth->GetDesc();
    if (source.Type != RESOURCE_DIM_TEX_2D || source.Format != TEX_FORMAT_RGBA16_FLOAT ||
        source.SampleCount != 1 || source.MipLevels != 1 || source.ArraySize != 1 ||
        !(source.BindFlags & BIND_RENDER_TARGET) || d.Type != RESOURCE_DIM_TEX_2D ||
        d.Format != TEX_FORMAT_D32_FLOAT || d.SampleCount != 1 || d.MipLevels != 1 ||
        d.ArraySize != 1 || !(d.BindFlags & BIND_DEPTH_STENCIL) ||
        !(d.BindFlags & BIND_SHADER_RESOURCE) || d.Width != source.Width ||
        d.Height != source.Height || srv.ViewType != TEXTURE_VIEW_SHADER_RESOURCE ||
        srv.TextureDim != RESOURCE_DIM_TEX_2D || srv.Format != TEX_FORMAT_R32_FLOAT ||
        srv.MostDetailedMip != 0 || srv.NumMipLevels != 1 || srv.FirstArraySlice != 0 ||
        srv.NumArraySlices != 1 || color == snapshot_.RawPtr() || color == filtered_.RawPtr())
        throw std::invalid_argument("Depth of field requires matching complete single-sample "
                                    "2D HDR/depth targets without private-target alias");
    const auto plan = depth_of_field_plan(
        source.Width, source.Height, view,
        presentation_.device()->GetAdapterInfo().Texture.MaxTexture2DDimension, limit_);
    pipeline();
    if (snapshot_ && snapshot_->GetDesc().Width == plan.area.width &&
        snapshot_->GetDesc().Height == plan.area.height)
        return;
    TextureDesc desc;
    desc.Name = "FORGE focus view snapshot";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = plan.area.width;
    desc.Height = plan.area.height;
    desc.Format = TEX_FORMAT_RGBA16_FLOAT;
    desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
    RefCntAutoPtr<ITexture> snapshot, filtered;
    presentation_.device()->CreateTexture(desc, nullptr, &snapshot);
    desc.Name = "FORGE focused HDR";
    presentation_.device()->CreateTexture(desc, nullptr, &filtered);
    if (!snapshot || !filtered)
        throw std::runtime_error("Depth of field target allocation failed; old pair retained");
    snapshot_ = std::move(snapshot);
    filtered_ = std::move(filtered);
    bytes_ = plan.bytes;
    allocations_ += 2;
}
void DepthOfField::apply(IDeviceContext* context, ITexture* color, ITextureView* depth,
                         const CameraView& view, const SceneDepthOfField& settings) {
    prepare(color, depth, view, settings);
    if (!settings.enabled || settings.blur_radius == 0)
        return;
    if (!context)
        throw std::invalid_argument("Depth of field requires its presentation context");
    const auto& area = view.viewport;
    {
        MapHelper<float> data(context, values_, MAP_WRITE, MAP_FLAG_DISCARD);
        data[0] = float(area.x);
        data[1] = float(area.y);
        data[2] = float(area.width);
        data[3] = float(area.height);
        data[4] = view.projection[10];
        data[5] = view.projection[11];
        data[6] = view.projection[14];
        data[7] = view.projection[15];
        data[8] = settings.focus_distance;
        data[9] = settings.focus_range;
        data[10] = settings.blur_radius;
        data[11] = 0;
    }
    Box box{area.x, area.x + area.width, area.y, area.y + area.height};
    CopyTextureAttribs copy{color, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, snapshot_,
                            RESOURCE_STATE_TRANSITION_MODE_TRANSITION};
    copy.pSrcBox = &box;
    context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
    context->CopyTexture(copy);
    binding_->GetVariableByName(SHADER_TYPE_PIXEL, "FocusColor")
        ->Set(snapshot_->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
    binding_->GetVariableByName(SHADER_TYPE_PIXEL, "FocusDepth")->Set(depth);
    auto* target = filtered_->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
    context->SetRenderTargets(1, &target, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    Diligent::Viewport viewport{0, 0, float(area.width), float(area.height), 0, 1};
    context->SetViewports(1, &viewport, area.width, area.height);
    context->SetPipelineState(pipeline_);
    context->CommitShaderResources(binding_, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->Draw(DrawAttribs{3, DRAW_FLAG_VERIFY_ALL});
    context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
    CopyTextureAttribs result{filtered_, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, color,
                              RESOURCE_STATE_TRANSITION_MODE_TRANSITION};
    result.DstX = area.x;
    result.DstY = area.y;
    context->CopyTexture(result);
}
} // namespace forge
