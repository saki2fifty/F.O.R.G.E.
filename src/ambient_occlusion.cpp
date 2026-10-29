#include "ambient_occlusion.hpp"
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include "presentation_views.hpp"
#include "render_projection.hpp"

namespace forge {
using namespace Diligent;
void AmbientOcclusion::prepare() {
    if (thread_ != std::this_thread::get_id())
        throw std::logic_error("Ambient occlusion requires its presentation thread");
    if (pipeline_)
        return;
    ShaderCreateInfo shader;
    shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    shader.EntryPoint = "main";
    shader.Desc.Name = "FORGE ambient triangle";
    shader.Desc.ShaderType = SHADER_TYPE_VERTEX;
    shader.Source = R"(float4 main(uint id:SV_VertexID):SV_Position {
        return float4(id==2?3:-1,id==1?3:-1,0,1);})";
    RefCntAutoPtr<IShader> vertex, pixel;
    presentation_.shader(shader, &vertex);
    shader.Desc.Name = "FORGE bounded indirect occlusion";
    shader.Desc.ShaderType = SHADER_TYPE_PIXEL;
    shader.Source = R"(
Texture2D<float4> CurrentColor;
Texture2D<float4> CurrentIndirect;
Texture2D<float> CurrentDepth;
cbuffer AmbientValues {float4 Extent;float4 Settings;float4 P0;float4 P1;float4 P2;float4 P3;};
bool positionAt(int2 p,out float3 location) {
    location=0;
    if(any(p<0)||any(p>=int2(Extent.xy)))return false;
    float d=CurrentDepth.Load(int3(p,0));
    if(!(d>=0&&d<1))return false;
    float denominator=d*P3.z-P2.z;
    if(abs(denominator)<=1e-20)return false;
    float z=(P2.w-d*P3.w)/denominator;
    float w=P3.z*z+P3.w;
    float2 ndc=(float2(p)+.5)/Extent.xy*float2(2,-2)+float2(-1,1);
    location=float3((ndc.x*w-P0.z*z-P0.w)/P0.x,(ndc.y*w-P1.z*z-P1.w)/P1.y,z);
    return z>0&&w>0&&all(isfinite(location));
}
bool differential(int2 p,int2 step,float3 center,out float3 delta) {
    float3 a,b;bool left=positionAt(p-step,a),right=positionAt(p+step,b);
    if(!left&&!right){delta=0;return false;}
    // Select the local side at a silhouette rather than bridging a depth step.
    delta=right&&(!left||abs(b.z-center.z)<=abs(a.z-center.z))?b-center:center-a;
    return all(isfinite(delta));
}
float4 main(float4 position:SV_Position):SV_Target {
    int2 p=int2(position.xy);
    float4 original=CurrentColor.Load(int3(p,0));
    float3 indirect=CurrentIndirect.Load(int3(p,0)).rgb;
    if(!all(isfinite(indirect))||!any(indirect!=0))return original;
    float3 center,dx,dy;
    if(!positionAt(p,center)||!differential(p,int2(1,0),center,dx)||
       !differential(p,int2(0,1),center,dy))return original;
    float3 n=cross(dx,dy);float magnitude=length(n);
    if(!isfinite(magnitude)||magnitude<=1e-20)return original;
    n/=magnitude;
    float3 towardEye=P3.z==0?float3(0,0,-1):-center;
    if(dot(n,towardEye)<0)n=-n;
    float3 tangent=normalize(cross(n,abs(n.z)<.9?float3(0,0,1):float3(0,1,0)));
    float3 bitangent=cross(n,tangent);
    float occluded=0;
    const float turn=6.283185307179586;
    float rotation=((p.x+3*p.y)&3)*(.25*turn);
    [unroll]for(int i=0;i<16;++i) {
        float height=(float(i)+.5)/16;
        float radial=sqrt(max(0.,1-height*height));
        float angle=((i*7)%16)*(turn/16)+rotation;
        float3 direction=tangent*(cos(angle)*radial)+bitangent*(sin(angle)*radial)+n*height;
        float3 samplePoint=center+direction*(Extent.z*(.2+.8*height*height));
        float4 v=float4(samplePoint,1);
        float4 clip=float4(dot(P0,v),dot(P1,v),dot(P2,v),dot(P3,v));
        if(!all(isfinite(clip))||clip.w<=0||clip.z<0||clip.z>clip.w)continue;
        float2 uv=clip.xy/clip.w*float2(.5,-.5)+.5;
        if(any(uv<0)||any(uv>=1))continue;
        float3 surface;
        if(!positionAt(int2(uv*Extent.xy),surface)||surface.z>=samplePoint.z-Settings.x)continue;
        float3 offset=surface-center;float distance=length(offset);
        if(!isfinite(distance)||distance<=1e-8||distance>=Extent.z||dot(offset,n)<=Settings.x)continue;
        occluded+=saturate(dot(n,offset)/distance)*(1-distance/Extent.z);
    }
    // Uniform hemisphere quadrature of cosine-weighted occlusion. The surface
    // attachment contains only environment radiance after layer/fog attenuation.
    float amount=saturate(2*occluded/16)*Extent.w;
    if(amount==0)return original;
    return float4(max(original.rgb-indirect*amount,0),original.a);
})";
    presentation_.shader(shader, &pixel);
    BufferDesc desc;
    desc.Name = "FORGE ambient view parameters";
    desc.Size = 96;
    desc.BindFlags = BIND_UNIFORM_BUFFER;
    desc.Usage = USAGE_DYNAMIC;
    desc.CPUAccessFlags = CPU_ACCESS_WRITE;
    RefCntAutoPtr<IBuffer> values;
    presentation_.device()->CreateBuffer(desc, nullptr, &values);
    if (!values)
        throw std::runtime_error("Ambient parameter allocation failed");
    GraphicsPipelineStateCreateInfo info;
    info.PSODesc.Name = "FORGE surface indirect occlusion";
    info.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    const ShaderResourceVariableDesc inputs[]{
        {SHADER_TYPE_PIXEL, "CurrentColor", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
        {SHADER_TYPE_PIXEL, "CurrentIndirect", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
        {SHADER_TYPE_PIXEL, "CurrentDepth", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC}};
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
        throw std::runtime_error("Ambient pipeline is unavailable");
    pipeline->CreateShaderResourceBinding(&binding, true);
    if (!binding || !binding->GetVariableByName(SHADER_TYPE_PIXEL, "AmbientValues"))
        throw std::runtime_error("Ambient shader interface is incomplete");
    for (const auto& input : inputs)
        if (!binding->GetVariableByName(SHADER_TYPE_PIXEL, input.Name))
            throw std::runtime_error("Ambient texture interface is incomplete");
    binding->GetVariableByName(SHADER_TYPE_PIXEL, "AmbientValues")->Set(values);
    pipeline_ = std::move(pipeline);
    binding_ = std::move(binding);
    values_ = std::move(values);
}
void AmbientOcclusion::apply(IDeviceContext* context, const Input& input, ITextureView* output) {
    validate_scene_ambient_occlusion(input.settings);
    if (thread_ != std::this_thread::get_id() || !context ||
        !complete_surface_view(input.color, TEX_FORMAT_RGBA16_FLOAT,
                               TEXTURE_VIEW_SHADER_RESOURCE) ||
        !complete_surface_view(output, TEX_FORMAT_RGBA16_FLOAT, TEXTURE_VIEW_RENDER_TARGET))
        throw std::invalid_argument(
            "Ambient occlusion requires its thread/context and complete HDR views");
    const auto& size = input.color->GetTexture()->GetDesc();
    if (output->GetTexture() == input.color->GetTexture() ||
        output->GetTexture()->GetDesc().Width != size.Width ||
        output->GetTexture()->GetDesc().Height != size.Height)
        throw std::invalid_argument("Ambient output must be distinct with matching extent");
    if (!input.settings.enabled || input.settings.radius == 0 || input.settings.strength == 0) {
        context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
        context->CopyTexture({input.color->GetTexture(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                              output->GetTexture(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION});
        return;
    }
    if (!complete_surface_view(input.indirect, TEX_FORMAT_RGBA16_FLOAT,
                               TEXTURE_VIEW_SHADER_RESOURCE) ||
        !complete_surface_view(input.depth, TEX_FORMAT_D32_FLOAT, TEXTURE_VIEW_SHADER_RESOURCE))
        throw std::invalid_argument(
            "Ambient occlusion requires current HDR indirect radiance and D32 depth");
    for (auto* view : {input.indirect, input.depth}) {
        const auto& d = view->GetTexture()->GetDesc();
        if (d.Width != size.Width || d.Height != size.Height ||
            view->GetTexture() == output->GetTexture())
            throw std::invalid_argument("Ambient input extent or output alias is invalid");
    }
    const auto& p = input.projection;
    if (!std::all_of(p.begin(), p.end(), [](float x) { return std::isfinite(x); }) || p[0] == 0 ||
        p[5] == 0 || p[1] != 0 || p[4] != 0 || p[8] != 0 || p[9] != 0 || p[12] != 0 || p[13] != 0)
        throw std::invalid_argument("Ambient projection must use the admitted camera axes");
    validate_forward_depth_projection({p[10], p[11], p[14], p[15]});
    prepare();
    {
        MapHelper<float> data(context, values_, MAP_WRITE, MAP_FLAG_DISCARD);
        data[0] = float(size.Width);
        data[1] = float(size.Height);
        data[2] = input.settings.radius;
        data[3] = input.settings.strength;
        data[4] = input.settings.bias;
        data[5] = data[6] = data[7] = 0;
        for (unsigned i = 0; i < 16; ++i)
            data[8 + i] = p[i];
    }
    const char* names[]{"CurrentColor", "CurrentIndirect", "CurrentDepth"};
    ITextureView* views[]{input.color, input.indirect, input.depth};
    for (unsigned i = 0; i < 3; ++i)
        binding_->GetVariableByName(SHADER_TYPE_PIXEL, names[i])->Set(views[i]);
    context->SetRenderTargets(1, &output, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    Diligent::Viewport viewport{0, 0, float(size.Width), float(size.Height), 0, 1};
    context->SetViewports(1, &viewport, size.Width, size.Height);
    context->SetPipelineState(pipeline_);
    context->CommitShaderResources(binding_, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->Draw(DrawAttribs{3, DRAW_FLAG_VERIFY_ALL});
    for (const auto* name : names)
        binding_->GetVariableByName(SHADER_TYPE_PIXEL, name)->Set(nullptr);
}
} // namespace forge
