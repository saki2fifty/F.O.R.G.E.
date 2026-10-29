#include "surface_reflections.hpp"
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include "presentation_views.hpp"
#include "reflection_source_shader.hpp"
#include "render_projection.hpp"
#include "surface_depth_shader.hpp"
namespace forge {
using namespace Diligent;
void SurfaceReflections::prepare(bool filtered) {
    if (thread_ != std::this_thread::get_id())
        throw std::logic_error("Screen-space reflections require their presentation thread");
    auto& mode = screens_[filtered ? 1 : 0];
    if (mode.pipeline)
        return;
    ShaderCreateInfo shader;
    shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    shader.EntryPoint = "main";
    shader.Desc.Name = "FORGE reflection triangle";
    shader.Desc.ShaderType = SHADER_TYPE_VERTEX;
    shader.Source = R"(float4 main(uint id:SV_VertexID):SV_Position {
        return float4(id==2?3:-1,id==1?3:-1,0,1);})";
    RefCntAutoPtr<IShader> vertex, pixel;
    presentation_.shader(shader, &vertex);
    const std::string source = std::string(filtered ? "#define FORGE_REFLECTION_FILTERED 1\n"
                                                    : "#define FORGE_REFLECTION_FILTERED 0\n") +
                               std::string(R"(
Texture2D<float4> CurrentColor;
Texture2D<float4> CurrentIndirect;
Texture2D<float> CurrentDepth;
Texture2D<float4> CurrentSurface;
Texture2D<float4> CurrentResponse;
Texture2D<float4> CurrentSpecular;
cbuffer ReflectionValues {float4 Extent;float4 Settings;float4 P0;float4 P1;float4 P2;float4 P3;};
)") + (filtered ? reflection_source_lookup_shader() : std::string{}) +
                               std::string(surface_depth_shader) +
                               R"(
bool projected(float3 pointAt,out float2 uv) {
    uv=0;float4 p=float4(pointAt,1);
    float4 clip=float4(dot(P0,p),dot(P1,p),dot(P2,p),dot(P3,p));
    if(!all(isfinite(clip))||clip.w<=0||clip.z<0||clip.z>=clip.w)return false;
    uv=clip.xy/clip.w*float2(.5,-.5)+.5;
    return all(uv>=0)&&all(uv<1);
}
bool sampleRay(float3 origin,float3 direction,float distance,out float2 uv,out float gap) {
    gap=-1;float3 at=origin+direction*distance,geometry;
    if(!projected(at,uv)||!positionAt(int2(uv*Extent.xy),geometry))return false;
    gap=at.z-geometry.z;
    return isfinite(gap);
}
struct Result {float4 Color:SV_Target0;float4 Indirect:SV_Target1;};
Result main(float4 position:SV_Position) {
    int2 p=int2(position.xy);Result result;
    result.Color=CurrentColor.Load(int3(p,0));
    result.Indirect=CurrentIndirect.Load(int3(p,0));
    float4 surface=CurrentSurface.Load(int3(p,0));
    float3 response=CurrentResponse.Load(int3(p,0)).rgb;
    float3 previous=CurrentSpecular.Load(int3(p,0)).rgb;
    float3 receiver;
    if(!all(isfinite(surface))||!all(isfinite(response))||!all(isfinite(previous))||
       any(response<0)||any(previous<0)||!any(response>0)||surface.a<0||
       surface.a>=Settings.y||!positionAt(p,receiver))return result;
    float lengthN=length(surface.xyz);
    if(!isfinite(lengthN)||lengthN<=1e-10)return result;
    float3 n=surface.xyz/lengthN;
    float3 incoming=P3.z==0?float3(0,0,1):normalize(receiver);
    if(dot(n,-incoming)<=.001)return result;
    float3 direction=reflect(incoming,n);
    float3 origin=receiver+n*max(.001,Extent.w*.1);
    float last=0;
    [loop]for(int iteration=1;iteration<=64;++iteration) {
        float fraction=float(iteration)/64;
        float distance=Extent.z*fraction*fraction;
        float2 uv;float gap;
        if(!projected(origin+direction*distance,uv))break;
        bool available=sampleRay(origin,direction,distance,uv,gap);
        if(available&&gap>=0) {
            float low=last,high=distance;
            [unroll]for(int refine=0;refine<8;++refine) {
                float middle=(low+high)*.5;float2 testUV;float testGap;
                if(sampleRay(origin,direction,middle,testUV,testGap)&&testGap>=0)high=middle;
                else low=middle;
            }
            if(sampleRay(origin,direction,high,uv,gap)&&gap>=0&&gap<=Extent.w&&
               length(uv*Extent.xy-position.xy)>=2) {
                float3 radiance=CurrentColor.Load(int3(int2(uv*Extent.xy),0)).rgb;
                if(!all(isfinite(radiance))||any(radiance<0))return result;
                float coverage=1;
#if FORGE_REFLECTION_FILTERED
                if(!all(isfinite(result.Color.rgb))||!all(isfinite(result.Indirect.rgb))||
                   any(result.Color.rgb<0)||any(result.Indirect.rgb<0))return result;
                float4 filtered=sourceRadiance(uv,origin+direction*high,high,surface.a,radiance);
                radiance=filtered.rgb;coverage=filtered.a;
#endif
                float border=min(min(uv.x,uv.y),min(1-uv.x,1-uv.y));
                float rough=1-smoothstep(Settings.y*.6,Settings.y,surface.a);
                float confidence=saturate(border*20)*rough*(1-high/Extent.z)*
                                 saturate(1-gap/Extent.w)*Settings.x*coverage;
                if(confidence==0)return result;
                float3 change=(radiance*response-previous)*confidence;
#if FORGE_REFLECTION_FILTERED
                // Preserve the same delta in both half-float outputs, including
                // finite HDR headroom, rather than clipping them independently.
                change=clamp(change,-min(result.Color.rgb,result.Indirect.rgb),
                             min(65504.-result.Color.rgb,65504.-result.Indirect.rgb));
#endif
                result.Color.rgb=max(result.Color.rgb+change,0);
                result.Indirect.rgb=max(result.Indirect.rgb+change,0);
                return result;
            }
        }
        last=distance;
    }
    return result;
})";
    shader.Desc.Name = "FORGE current surface reflection";
    shader.Desc.ShaderType = SHADER_TYPE_PIXEL;
    shader.Source = source.c_str();
    presentation_.shader(shader, &pixel);
    BufferDesc desc;
    desc.Name = "FORGE reflection parameters";
    desc.Size = 96;
    desc.BindFlags = BIND_UNIFORM_BUFFER;
    desc.Usage = USAGE_DYNAMIC;
    desc.CPUAccessFlags = CPU_ACCESS_WRITE;
    RefCntAutoPtr<IBuffer> values;
    presentation_.device()->CreateBuffer(desc, nullptr, &values);
    if (!values)
        throw std::runtime_error("Reflection parameter allocation failed");
    std::vector<ShaderResourceVariableDesc> inputs{
        {SHADER_TYPE_PIXEL, "CurrentColor", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
        {SHADER_TYPE_PIXEL, "CurrentIndirect", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
        {SHADER_TYPE_PIXEL, "CurrentDepth", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
        {SHADER_TYPE_PIXEL, "CurrentSurface", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
        {SHADER_TYPE_PIXEL, "CurrentResponse", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
        {SHADER_TYPE_PIXEL, "CurrentSpecular", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC}};
    std::array<std::string, reflection_source_level_limit * 2> source_names;
    if (filtered)
        for (unsigned i = 0; i < reflection_source_level_limit; ++i) {
            source_names[i * 2] = reflection_source_name(i, false);
            source_names[i * 2 + 1] = reflection_source_name(i, true);
            for (unsigned lane = 0; lane < 2; ++lane)
                inputs.push_back({SHADER_TYPE_PIXEL, source_names[i * 2 + lane].c_str(),
                                  SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC});
        }
    GraphicsPipelineStateCreateInfo info;
    info.PSODesc.Name = "FORGE current color and indirect reflections";
    info.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    info.PSODesc.ResourceLayout.Variables = inputs.data();
    info.PSODesc.ResourceLayout.NumVariables = Uint32(std::size(inputs));
    auto& g = info.GraphicsPipeline;
    g.NumRenderTargets = 2;
    g.RTVFormats[0] = g.RTVFormats[1] = TEX_FORMAT_RGBA16_FLOAT;
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
        throw std::runtime_error("Reflection pipeline is unavailable");
    pipeline->CreateShaderResourceBinding(&binding, true);
    if (!binding || !binding->GetVariableByName(SHADER_TYPE_PIXEL, "ReflectionValues"))
        throw std::runtime_error("Reflection shader interface is incomplete");
    for (const auto& input : inputs)
        if (!binding->GetVariableByName(SHADER_TYPE_PIXEL, input.Name))
            throw std::runtime_error("Reflection texture interface is incomplete");
    binding->GetVariableByName(SHADER_TYPE_PIXEL, "ReflectionValues")->Set(values);
    mode.pipeline = std::move(pipeline);
    mode.binding = std::move(binding);
    mode.values = std::move(values);
}
void SurfaceReflections::apply(IDeviceContext* context, const Input& input,
                               ITextureView* color_output, ITextureView* indirect_output) {
    validate_scene_screen_space_reflections(input.settings);
    if (thread_ != std::this_thread::get_id() || !context ||
        !complete_surface_view(input.color, TEX_FORMAT_RGBA16_FLOAT,
                               TEXTURE_VIEW_SHADER_RESOURCE) ||
        !complete_surface_view(input.indirect, TEX_FORMAT_RGBA16_FLOAT,
                               TEXTURE_VIEW_SHADER_RESOURCE) ||
        !complete_surface_view(color_output, TEX_FORMAT_RGBA16_FLOAT, TEXTURE_VIEW_RENDER_TARGET) ||
        !complete_surface_view(indirect_output, TEX_FORMAT_RGBA16_FLOAT,
                               TEXTURE_VIEW_RENDER_TARGET))
        throw std::invalid_argument(
            "Reflections require their thread/context and complete HDR inputs/results");
    const auto& size = input.color->GetTexture()->GetDesc();
    const auto matches = [&](ITextureView* view) {
        return view->GetTexture()->GetDesc().Width == size.Width &&
               view->GetTexture()->GetDesc().Height == size.Height;
    };
    if (!matches(input.indirect) || !matches(color_output) || !matches(indirect_output) ||
        color_output->GetTexture() == indirect_output->GetTexture())
        throw std::invalid_argument("Reflection results must be distinct with matching extent");
    for (auto* source : {input.color, input.indirect})
        if (source->GetTexture() == color_output->GetTexture() ||
            source->GetTexture() == indirect_output->GetTexture())
            throw std::invalid_argument("Reflection results cannot alias current inputs");
    if (!input.settings.enabled || input.settings.strength == 0 ||
        input.settings.max_distance == 0 || input.settings.max_roughness == 0) {
        context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
        context->CopyTexture({input.color->GetTexture(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                              color_output->GetTexture(),
                              RESOURCE_STATE_TRANSITION_MODE_TRANSITION});
        context->CopyTexture(
            {input.indirect->GetTexture(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
             indirect_output->GetTexture(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION});
        return;
    }
    const auto validate_input = [&](ITextureView* source, TEXTURE_FORMAT format) {
        if (!complete_surface_view(source, format, TEXTURE_VIEW_SHADER_RESOURCE) ||
            !matches(source) || source->GetTexture() == color_output->GetTexture() ||
            source->GetTexture() == indirect_output->GetTexture())
            throw std::invalid_argument(
                "Reflection material/depth inputs have invalid type, extent or output alias");
    };
    validate_input(input.depth, TEX_FORMAT_D32_FLOAT);
    for (auto* source : {input.surface, input.response, input.specular})
        validate_input(source, TEX_FORMAT_RGBA16_FLOAT);
    validate_surface_projection(input.projection);
    const bool filtered = input.settings.roughness_filtering &&
                          reflection_source_plan(size.Width, size.Height).count != 0;
    if (filtered) {
        validate_sources(input, input.sources);
        for (const auto& source : input.sources)
            for (auto* texture : {source.color, source.range})
                if (texture == color_output->GetTexture() ||
                    texture == indirect_output->GetTexture())
                    throw std::invalid_argument("Reflection source aliases an output");
    }
    prepare(filtered);
    auto& mode = screens_[filtered ? 1 : 0];
    {
        MapHelper<float> data(context, mode.values, MAP_WRITE, MAP_FLAG_DISCARD);
        data[0] = float(size.Width);
        data[1] = float(size.Height);
        data[2] = input.settings.max_distance;
        data[3] = input.settings.thickness;
        data[4] = input.settings.strength;
        data[5] = input.settings.max_roughness;
        data[6] = filtered ? 1.f : 0.f;
        data[7] = float(input.sources.size());
        for (unsigned i = 0; i < 16; ++i)
            data[8 + i] = input.projection[i];
    }
    const char* names[]{"CurrentColor",   "CurrentIndirect", "CurrentDepth",
                        "CurrentSurface", "CurrentResponse", "CurrentSpecular"};
    ITextureView* inputs[]{input.color,   input.indirect, input.depth,
                           input.surface, input.response, input.specular};
    for (unsigned i = 0; i < std::size(inputs); ++i)
        mode.binding->GetVariableByName(SHADER_TYPE_PIXEL, names[i])->Set(inputs[i]);
    if (filtered)
        for (unsigned i = 0; i < reflection_source_level_limit; ++i) {
            const auto& level = input.sources[std::min<std::size_t>(i, input.sources.size() - 1)];
            mode.binding
                ->GetVariableByName(SHADER_TYPE_PIXEL, reflection_source_name(i, false).c_str())
                ->Set(level.color->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
            mode.binding
                ->GetVariableByName(SHADER_TYPE_PIXEL, reflection_source_name(i, true).c_str())
                ->Set(level.range->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
        }
    ITextureView* outputs[]{color_output, indirect_output};
    context->SetRenderTargets(2, outputs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    Diligent::Viewport viewport{0, 0, float(size.Width), float(size.Height), 0, 1};
    context->SetViewports(1, &viewport, size.Width, size.Height);
    context->SetPipelineState(mode.pipeline);
    context->CommitShaderResources(mode.binding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->Draw(DrawAttribs{3, DRAW_FLAG_VERIFY_ALL});
    if (filtered)
        for (unsigned i = 0; i < reflection_source_level_limit; ++i)
            for (bool depth : {false, true})
                mode.binding
                    ->GetVariableByName(SHADER_TYPE_PIXEL, reflection_source_name(i, depth).c_str())
                    ->Set(nullptr);
    for (const auto* name : names)
        mode.binding->GetVariableByName(SHADER_TYPE_PIXEL, name)->Set(nullptr);
}
} // namespace forge
