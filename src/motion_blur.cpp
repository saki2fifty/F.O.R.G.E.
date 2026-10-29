#include "motion_blur.hpp"
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include "render_projection.hpp"

namespace forge {
using namespace Diligent;
void MotionBlur::prepare() {
    if (thread_ != std::this_thread::get_id())
        throw std::logic_error("Motion blur requires its presentation thread");
    if (pipeline_)
        return;
    ShaderCreateInfo shader;
    shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    shader.EntryPoint = "main";
    shader.Desc.Name = "FORGE motion blur triangle";
    shader.Desc.ShaderType = SHADER_TYPE_VERTEX;
    shader.Source = R"(float4 main(uint id:SV_VertexID):SV_Position {
        return float4(id==2?3:-1,id==1?3:-1,0,1);
    })";
    RefCntAutoPtr<IShader> vertex, pixel;
    presentation_.shader(shader, &vertex);
    shader.Desc.Name = "FORGE surface velocity blur";
    shader.Desc.ShaderType = SHADER_TYPE_PIXEL;
    shader.Source = R"(
Texture2D<float4> MotionColor;
Texture2D<float4> SurfaceMotion;
Texture2D<float> CurrentDepth;
Texture2D<float> MotionDepth;
cbuffer MotionValues {float4 Extent;float4 Projection;float4 MotionGrid;};
float positiveDepth(float d) {
    float divisor=d*Projection.z-Projection.x;
    if(abs(divisor)<=1e-20)return -1;
    float z=(Projection.y-d*Projection.w)/divisor;
    return isfinite(z)&&z>0?z:-1;
}
bool surface(int2 p,out float z,out float2 velocity) {
    z=-1;velocity=0;
    int2 size=int2(Extent.xy);
    if(any(p<0)||any(p>=size))return false;
    float raw=CurrentDepth.Load(int3(p,0));
    if(!(raw>=0&&raw<1))return false;
    z=positiveDepth(raw);if(z<=0)return false;
    int2 at=int2(floor(float2(p)+.5+MotionGrid.xy));
    if(any(at<0)||any(at>=size))return false;
    float4 movement=SurfaceMotion.Load(int3(at,0));
    float sourceRaw=MotionDepth.Load(int3(at,0));
    float sourceDepth=positiveDepth(sourceRaw);
    if(!all(isfinite(movement))||movement.w<.5||movement.z<0||movement.z>1||
       !(sourceRaw>=0&&sourceRaw<1)||sourceDepth<=0||
       abs(sourceDepth-z)>max(.002,.02*z))return false;
    velocity=movement.xy*float2(.5,-.5)*Extent.xy;
    return all(isfinite(velocity));
}
float4 main(float4 position:SV_Position):SV_Target {
    int2 p=int2(position.xy);
    float4 original=MotionColor.Load(int3(p,0));
    float z;float2 velocity;
    if(!surface(p,z,velocity))return original;
    float2 trail=velocity*Extent.z;
    float distance=length(trail);
    if(!isfinite(distance)||distance<.5)return original;
    if(distance>Extent.w)trail*=Extent.w/distance;
    distance=min(distance,Extent.w);
    if(distance<.5)return original;
    int segments=max(1,min(32,int(ceil(distance))));
    float3 sum=0;float weight=0;
    // Exposure ending at this image: a translating image at an earlier instant
    // is currentColor(p+velocity*t), not the AA history lookup p-velocity.
    [loop] for(int step=0;step<=32;++step) {
        if(step>segments)break;
        float2 samplePosition=float2(p)+trail*(float(step)/segments);
        int2 base=int2(floor(samplePosition));float2 f=frac(samplePosition);
        float edge=(step==0||step==segments)?.5:1;
        [unroll]for(int y=0;y<2;++y)[unroll]for(int x=0;x<2;++x) {
            int2 at=base+int2(x,y);float sampleZ;float2 sampleVelocity;
            if(!surface(at,sampleZ,sampleVelocity))continue;
            if(abs(sampleZ-z)>max(.002,.02*z)||
               length(sampleVelocity-velocity)>max(1.,.25*length(velocity)))continue;
            float w=edge*(x?f.x:1-f.x)*(y?f.y:1-f.y);
            sum+=MotionColor.Load(int3(at,0)).rgb*w;weight+=w;
        }
    }
    return weight>0?float4(sum/weight,original.a):original;
})";
    presentation_.shader(shader, &pixel);
    BufferDesc desc;
    desc.Name = "FORGE surface motion parameters";
    desc.Size = 48;
    desc.BindFlags = BIND_UNIFORM_BUFFER;
    desc.Usage = USAGE_DYNAMIC;
    desc.CPUAccessFlags = CPU_ACCESS_WRITE;
    RefCntAutoPtr<IBuffer> values;
    presentation_.device()->CreateBuffer(desc, nullptr, &values);
    if (!values)
        throw std::runtime_error("Motion blur parameter allocation failed");
    GraphicsPipelineStateCreateInfo info;
    info.PSODesc.Name = "FORGE depth-aware surface motion blur";
    info.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    const ShaderResourceVariableDesc inputs[]{
        {SHADER_TYPE_PIXEL, "MotionColor", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
        {SHADER_TYPE_PIXEL, "SurfaceMotion", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
        {SHADER_TYPE_PIXEL, "CurrentDepth", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
        {SHADER_TYPE_PIXEL, "MotionDepth", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC}};
    info.PSODesc.ResourceLayout.Variables = inputs;
    info.PSODesc.ResourceLayout.NumVariables = Uint32(std::size(inputs));
    auto& g = info.GraphicsPipeline;
    g.NumRenderTargets = 1;
    g.RTVFormats[0] = TEX_FORMAT_RGBA16_FLOAT;
    g.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    g.RasterizerDesc.CullMode = CULL_MODE_NONE;
    g.DepthStencilDesc.DepthEnable = false;
    g.DepthStencilDesc.DepthWriteEnable = false;
    info.pVS = vertex;
    info.pPS = pixel;
    RefCntAutoPtr<IPipelineState> pipeline;
    RefCntAutoPtr<IShaderResourceBinding> binding;
    presentation_.graphics(info, &pipeline);
    if (!pipeline)
        throw std::runtime_error("Motion blur pipeline is unavailable");
    pipeline->CreateShaderResourceBinding(&binding, true);
    if (!binding || !binding->GetVariableByName(SHADER_TYPE_PIXEL, "MotionValues"))
        throw std::runtime_error("Motion blur shader interface is incomplete");
    for (const auto& input : inputs)
        if (!binding->GetVariableByName(SHADER_TYPE_PIXEL, input.Name))
            throw std::runtime_error("Motion blur texture interface is incomplete");
    binding->GetVariableByName(SHADER_TYPE_PIXEL, "MotionValues")->Set(values);
    pipeline_ = std::move(pipeline);
    binding_ = std::move(binding);
    values_ = std::move(values);
}
void MotionBlur::apply(IDeviceContext* context, const Input& input, ITextureView* output) {
    validate_scene_motion_blur(input.settings);
    if (thread_ != std::this_thread::get_id() || !context || !input.color || !output)
        throw std::invalid_argument("Motion blur requires its thread, context, color and output");
    const auto complete = [](ITextureView* view, TEXTURE_FORMAT format, TEXTURE_VIEW_TYPE type) {
        if (!view)
            return false;
        const auto& texture = view->GetTexture()->GetDesc();
        const auto& v = view->GetDesc();
        const auto binding =
            type == TEXTURE_VIEW_SHADER_RESOURCE ? BIND_SHADER_RESOURCE : BIND_RENDER_TARGET;
        return texture.Type == RESOURCE_DIM_TEX_2D && texture.ArraySize == 1 &&
               texture.SampleCount == 1 && texture.MipLevels == 1 && texture.Format == format &&
               (texture.BindFlags & binding) != 0 && v.ViewType == type &&
               v.TextureDim == RESOURCE_DIM_TEX_2D && v.MostDetailedMip == 0 &&
               (type != TEXTURE_VIEW_SHADER_RESOURCE || v.NumMipLevels == 1) &&
               v.FirstArraySlice == 0 && v.NumArraySlices == 1 &&
               v.Format == (format == TEX_FORMAT_D32_FLOAT ? TEX_FORMAT_R32_FLOAT : format);
    };
    if (!complete(input.color, TEX_FORMAT_RGBA16_FLOAT, TEXTURE_VIEW_SHADER_RESOURCE) ||
        !complete(output, TEX_FORMAT_RGBA16_FLOAT, TEXTURE_VIEW_RENDER_TARGET))
        throw std::invalid_argument(
            "Motion blur color/output require complete single-sample HDR2D views");
    const auto& color = input.color->GetTexture()->GetDesc();
    if (output->GetTexture() == input.color->GetTexture() ||
        output->GetTexture()->GetDesc().Width != color.Width ||
        output->GetTexture()->GetDesc().Height != color.Height)
        throw std::invalid_argument("Motion blur output must be distinct with matching extent");
    if (!input.settings.enabled || input.settings.shutter == 0 || input.settings.max_pixels == 0) {
        context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
        context->CopyTexture({input.color->GetTexture(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                              output->GetTexture(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION});
        return;
    }
    auto* motion_depth = input.motion_depth ? input.motion_depth : input.depth;
    if (!complete(input.motion, TEX_FORMAT_RGBA32_FLOAT, TEXTURE_VIEW_SHADER_RESOURCE) ||
        !complete(input.depth, TEX_FORMAT_D32_FLOAT, TEXTURE_VIEW_SHADER_RESOURCE) ||
        !complete(motion_depth, TEX_FORMAT_D32_FLOAT, TEXTURE_VIEW_SHADER_RESOURCE))
        throw std::invalid_argument("Motion blur requires complete typed motion and depth views");
    for (auto* view : {input.motion, input.depth, motion_depth}) {
        const auto& d = view->GetTexture()->GetDesc();
        if (d.Width != color.Width || d.Height != color.Height ||
            view->GetTexture() == output->GetTexture())
            throw std::invalid_argument("Motion blur input extents or output alias are invalid");
    }
    validate_forward_depth_projection(input.projection);
    for (const auto offset : input.motion_offset)
        if (!std::isfinite(offset) || std::abs(offset) > 1)
            throw std::invalid_argument("Motion blur raster offset exceeds one pixel");
    prepare();
    {
        MapHelper<float> data(context, values_, MAP_WRITE, MAP_FLAG_DISCARD);
        data[0] = float(color.Width);
        data[1] = float(color.Height);
        data[2] = input.settings.shutter;
        data[3] = input.settings.max_pixels;
        for (unsigned i = 0; i < 4; ++i)
            data[4 + i] = input.projection[i];
        data[8] = input.motion_offset[0];
        data[9] = input.motion_offset[1];
        data[10] = data[11] = 0;
    }
    const char* names[]{"MotionColor", "SurfaceMotion", "CurrentDepth", "MotionDepth"};
    ITextureView* views[]{input.color, input.motion, input.depth, motion_depth};
    for (unsigned i = 0; i < 4; ++i)
        binding_->GetVariableByName(SHADER_TYPE_PIXEL, names[i])->Set(views[i]);
    context->SetRenderTargets(1, &output, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    Diligent::Viewport viewport{0, 0, float(color.Width), float(color.Height), 0, 1};
    context->SetViewports(1, &viewport, color.Width, color.Height);
    context->SetPipelineState(pipeline_);
    context->CommitShaderResources(binding_, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->Draw(DrawAttribs{3, DRAW_FLAG_VERIFY_ALL});
    for (const auto* name : names)
        binding_->GetVariableByName(SHADER_TYPE_PIXEL, name)->Set(nullptr);
}
} // namespace forge
