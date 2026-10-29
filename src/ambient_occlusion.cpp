#include "ambient_occlusion.hpp"
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include "presentation_views.hpp"
#include "render_projection.hpp"
#include "surface_depth_shader.hpp"

namespace forge {
using namespace Diligent;
void AmbientOcclusion::prepare(bool material_normals) {
    if (thread_ != std::this_thread::get_id())
        throw std::logic_error("Ambient occlusion requires its presentation thread");
    auto& mode = modes_[material_normals ? 1 : 0];
    if (mode.pipeline)
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
    const std::string source =
        std::string(material_normals ? "#define FORGE_MATERIAL_NORMALS 1\n"
                                     : "#define FORGE_MATERIAL_NORMALS 0\n") +
        std::string(R"(
Texture2D<float4> CurrentColor;
Texture2D<float4> CurrentIndirect;
Texture2D<float> CurrentDepth;
#if FORGE_MATERIAL_NORMALS
Texture2D<float4> CurrentSurface;
#endif
cbuffer AmbientValues {float4 Extent;float4 Settings;float4 P0;float4 P1;float4 P2;float4 P3;};
)") + std::string(surface_depth_shader) +
        R"(
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
#if FORGE_MATERIAL_NORMALS
    float3 geometricNormal=n;
    if(Settings.y>0) {
        float3 mapped=CurrentSurface.Load(int3(p,0)).xyz;
        float mappedLength=length(mapped);
        if(all(isfinite(mapped))&&isfinite(mappedLength)&&mappedLength>1e-8) {
            mapped/=mappedLength;
            if(dot(mapped,geometricNormal)>1e-4) {
                float3 mixed=lerp(n,mapped,Settings.y);
                float mixedLength=length(mixed);
                if(isfinite(mixedLength)&&mixedLength>1e-8)n=mixed/mixedLength;
            }
        }
    }
#endif
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
#if FORGE_MATERIAL_NORMALS
        // A shading normal must not make the underlying flat geometry occlude itself.
        if(dot(offset,geometricNormal)<=Settings.x)continue;
#endif
        occluded+=saturate(dot(n,offset)/distance)*(1-distance/Extent.z);
    }
    // Uniform hemisphere quadrature of cosine-weighted occlusion. The surface
    // attachment contains only environment radiance after layer/fog attenuation.
    float amount=saturate(2*occluded/16)*Extent.w;
    if(amount==0)return original;
    return float4(max(original.rgb-indirect*amount,0),original.a);
})";
    shader.Source = source.c_str();
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
        {SHADER_TYPE_PIXEL, "CurrentDepth", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
        {SHADER_TYPE_PIXEL, "CurrentSurface", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC}};
    info.PSODesc.ResourceLayout.Variables = inputs;
    const unsigned resource_count = material_normals ? 4 : 3;
    info.PSODesc.ResourceLayout.NumVariables = resource_count;
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
    for (unsigned i = 0; i < resource_count; ++i)
        if (!binding->GetVariableByName(SHADER_TYPE_PIXEL, inputs[i].Name))
            throw std::runtime_error("Ambient texture interface is incomplete");
    binding->GetVariableByName(SHADER_TYPE_PIXEL, "AmbientValues")->Set(values);
    mode.pipeline = std::move(pipeline);
    mode.binding = std::move(binding);
    mode.values = std::move(values);
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
    const bool material_normals = input.settings.normal_influence != 0;
    if (material_normals && (!complete_surface_view(input.surface, TEX_FORMAT_RGBA16_FLOAT,
                                                    TEXTURE_VIEW_SHADER_RESOURCE) ||
                             input.surface->GetTexture() == input.color->GetTexture() ||
                             input.surface->GetTexture() == input.indirect->GetTexture() ||
                             input.surface->GetTexture() == output->GetTexture() ||
                             input.surface->GetTexture()->GetDesc().Width != size.Width ||
                             input.surface->GetTexture()->GetDesc().Height != size.Height))
        throw std::invalid_argument(
            "Material normal occlusion requires a distinct complete current normal surface");
    const auto& p = input.projection;
    validate_surface_projection(p);
    prepare(material_normals);
    auto& mode = modes_[material_normals ? 1 : 0];
    {
        MapHelper<float> data(context, mode.values, MAP_WRITE, MAP_FLAG_DISCARD);
        data[0] = float(size.Width);
        data[1] = float(size.Height);
        data[2] = input.settings.radius;
        data[3] = input.settings.strength;
        data[4] = input.settings.bias;
        data[5] = input.settings.normal_influence;
        data[6] = data[7] = 0;
        for (unsigned i = 0; i < 16; ++i)
            data[8 + i] = p[i];
    }
    const char* names[]{"CurrentColor", "CurrentIndirect", "CurrentDepth", "CurrentSurface"};
    ITextureView* views[]{input.color, input.indirect, input.depth, input.surface};
    const unsigned resource_count = material_normals ? 4 : 3;
    for (unsigned i = 0; i < resource_count; ++i)
        mode.binding->GetVariableByName(SHADER_TYPE_PIXEL, names[i])->Set(views[i]);
    context->SetRenderTargets(1, &output, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    Diligent::Viewport viewport{0, 0, float(size.Width), float(size.Height), 0, 1};
    context->SetViewports(1, &viewport, size.Width, size.Height);
    context->SetPipelineState(mode.pipeline);
    context->CommitShaderResources(mode.binding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->Draw(DrawAttribs{3, DRAW_FLAG_VERIFY_ALL});
    for (unsigned i = 0; i < resource_count; ++i)
        mode.binding->GetVariableByName(SHADER_TYPE_PIXEL, names[i])->Set(nullptr);
}
} // namespace forge
