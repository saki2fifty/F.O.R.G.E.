#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include "Utilities/interface/DiligentFXShaderSourceStreamFactory.hpp"
#include "presentation_views.hpp"
#include "render_projection.hpp"
#include "surface_depth_shader.hpp"
#include "surface_reflections.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
namespace forge {
using namespace Diligent;
namespace {
constexpr unsigned probe_limit = 8, header_floats = 36, probe_floats = 24;
constexpr const char* surface_names[]{"CurrentColor",   "CurrentIndirect", "CurrentDepth",
                                      "CurrentSurface", "CurrentResponse", "CurrentSpecular"};
constexpr const char* cube_names[]{"Probe0", "Probe1", "Probe2", "Probe3",
                                   "Probe4", "Probe5", "Probe6", "Probe7"};
bool complete_probe_map(ITextureView* view) {
    if (!view)
        return false;
    const auto& image = view->GetTexture()->GetDesc();
    const auto& selection = view->GetDesc();
    return image.Type == RESOURCE_DIM_TEX_CUBE && image.ArraySize == 6 &&
           image.Width == image.Height && image.MipLevels != 0 && image.SampleCount == 1 &&
           (image.Format == TEX_FORMAT_RGBA16_FLOAT || image.Format == TEX_FORMAT_RGBA32_FLOAT) &&
           (image.BindFlags & BIND_SHADER_RESOURCE) != 0 &&
           selection.ViewType == TEXTURE_VIEW_SHADER_RESOURCE &&
           selection.TextureDim == RESOURCE_DIM_TEX_CUBE && selection.Format == image.Format &&
           selection.MostDetailedMip == 0 && selection.NumMipLevels == image.MipLevels &&
           selection.FirstArraySlice == 0 && selection.NumArraySlices == 6;
}
float packed(double value) {
    if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
        throw std::invalid_argument("Reflection probe geometry exceeds the current view profile");
    return float(value);
}
} // namespace
void SurfaceReflections::prepare_probes(bool specular_output) {
    if (thread_ != std::this_thread::get_id())
        throw std::logic_error("Local reflections require their presentation thread");
    auto& selected = probes_[specular_output ? 1 : 0];
    if (selected.pipeline)
        return;
    ShaderCreateInfo shader;
    shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    shader.EntryPoint = "main";
    shader.Desc.Name = "FORGE local reflection triangle";
    shader.Desc.ShaderType = SHADER_TYPE_VERTEX;
    shader.Source = R"(float4 main(uint id:SV_VertexID):SV_Position {
        return float4(id==2?3:-1,id==1?3:-1,0,1);})";
    RefCntAutoPtr<IShader> vertex, pixel;
    presentation_.shader(shader, &vertex);
    const std::string source =
        std::string(specular_output ? "#define STORE_SPECULAR 1\n" : "#define STORE_SPECULAR 0\n") +
        R"(
#include "PBR_Shading.fxh"
Texture2D<float4> CurrentColor;
Texture2D<float4> CurrentIndirect;
Texture2D<float> CurrentDepth;
Texture2D<float4> CurrentSurface;
Texture2D<float4> CurrentResponse;
Texture2D<float4> CurrentSpecular;
TextureCube Probe0;TextureCube Probe1;TextureCube Probe2;TextureCube Probe3;
TextureCube Probe4;TextureCube Probe5;TextureCube Probe6;TextureCube Probe7;
SamplerState ProbeSampler;
struct ProbeValues {float4 CenterIntensity;float4 Planes[3];float4 Options;float4 Flags;};
cbuffer LocalReflectionValues {
    float4 Extent;float4 P0;float4 P1;float4 P2;float4 P3;
    float4 Right;float4 Up;float4 Forward;float4 Count;ProbeValues Probes[8];
};
)" + std::string(surface_depth_shader) +
        R"(
float3 cubeAt(int index,float3 direction,float mip) {
    switch(index) {
        case 0:return SamplePrefilteredEnvMap(Probe0,ProbeSampler,direction,mip);
        case 1:return SamplePrefilteredEnvMap(Probe1,ProbeSampler,direction,mip);
        case 2:return SamplePrefilteredEnvMap(Probe2,ProbeSampler,direction,mip);
        case 3:return SamplePrefilteredEnvMap(Probe3,ProbeSampler,direction,mip);
        case 4:return SamplePrefilteredEnvMap(Probe4,ProbeSampler,direction,mip);
        case 5:return SamplePrefilteredEnvMap(Probe5,ProbeSampler,direction,mip);
        case 6:return SamplePrefilteredEnvMap(Probe6,ProbeSampler,direction,mip);
        default:return SamplePrefilteredEnvMap(Probe7,ProbeSampler,direction,mip);
    }
}
struct Result {
    float4 Color:SV_Target0;float4 Indirect:SV_Target1;
#if STORE_SPECULAR
    float4 Specular:SV_Target2;
#endif
};
Result main(float4 position:SV_Position) {
    int2 p=int2(position.xy);Result result;
    result.Color=CurrentColor.Load(int3(p,0));
    result.Indirect=CurrentIndirect.Load(int3(p,0));
    float4 old=CurrentSpecular.Load(int3(p,0));
#if STORE_SPECULAR
    result.Specular=old;
#endif
    float4 surface=CurrentSurface.Load(int3(p,0));
    float3 response=CurrentResponse.Load(int3(p,0)).rgb,viewPosition;
    if(!all(isfinite(surface))||!all(isfinite(response))||!all(isfinite(old.rgb))||
       any(response<0)||any(old.rgb<0)||!any(response>0)||surface.w<0||surface.w>1||
       !positionAt(p,viewPosition))return result;
    float lengthN=length(surface.xyz);
    if(!isfinite(lengthN)||lengthN<=1e-10)return result;
    float3 n=surface.xyz/lengthN;
    float3 towardEye=P3.z==0?float3(0,0,-1):normalize(-viewPosition);
    if(dot(n,towardEye)<=.001)return result;
    float3 worldPosition=Right.xyz*viewPosition.x+Up.xyz*viewPosition.y+
                         Forward.xyz*viewPosition.z;
    float3 worldNormal=normalize(Right.xyz*n.x+Up.xyz*n.y+Forward.xyz*n.z);
    float3 worldView=normalize(Right.xyz*towardEye.x+Up.xyz*towardEye.y+
                              Forward.xyz*towardEye.z);
    float3 ray=reflect(-worldView,worldNormal),local=0;
    float remaining=1;
    [loop]for(int index=0;index<int(Count.x)&&remaining>0;++index) {
        ProbeValues probe=Probes[index];float3 at=worldPosition-probe.CenterIntensity.xyz;
        float margin=3.402823466e+38;
        [unroll]for(int axis=0;axis<3;++axis)
            margin=min(margin,probe.Planes[axis].w-abs(dot(probe.Planes[axis].xyz,at)));
        if(margin<0)continue;
        float fade=probe.Options.x==0?1:saturate(margin/probe.Options.x);
        if(fade==0)continue;
        float3 direction=ray;
        if(probe.Flags.x!=0) {
            float exitDistance=3.402823466e+38;
            [unroll]for(int axis=0;axis<3;++axis) {
                float speed=dot(probe.Planes[axis].xyz,ray);
                if(abs(speed)>1e-8) {
                    float edge=speed>0?probe.Planes[axis].w:-probe.Planes[axis].w;
                    exitDistance=min(exitDistance,max(0,(edge-dot(probe.Planes[axis].xyz,at))/speed));
                }
            }
            float3 projectedDirection=at+ray*exitDistance;
            if(all(isfinite(projectedDirection))&&any(projectedDirection!=0))
                direction=projectedDirection;
        }
        direction=RotateDirectionAroundY(direction,probe.Options.yz);
        float3 radiance=cubeAt(index,direction,surface.w*probe.Options.w);
        if(!all(isfinite(radiance))||any(radiance<0))continue;
        float weight=remaining*fade;
        local+=radiance*response*(probe.CenterIntensity.w*weight);
        remaining*=1-fade;
    }
    float3 replacement=local+remaining*old.rgb;
    float3 change=replacement-old.rgb;
    if(!all(isfinite(replacement))||!all(isfinite(change)))return result;
    float3 nextColor=max(result.Color.rgb+change,0);
    float3 nextIndirect=max(result.Indirect.rgb+change,0);
    if(any(replacement>65504)||any(nextColor>65504)||any(nextIndirect>65504))return result;
    result.Color.rgb=nextColor;
    result.Indirect.rgb=nextIndirect;
#if STORE_SPECULAR
    result.Specular.rgb=replacement;
#endif
    return result;
})";
    shader.Desc.Name = "FORGE local surface reflections";
    shader.Desc.ShaderType = SHADER_TYPE_PIXEL;
    shader.pShaderSourceStreamFactory = &DiligentFXShaderSourceStreamFactory::GetInstance();
    shader.Source = source.c_str();
    presentation_.shader(shader, &pixel);
    PipelineState candidate;
    BufferDesc buffer;
    buffer.Name = "FORGE local reflection parameters";
    buffer.Size = sizeof(float) * (header_floats + probe_floats * probe_limit);
    buffer.BindFlags = BIND_UNIFORM_BUFFER;
    buffer.Usage = USAGE_DYNAMIC;
    buffer.CPUAccessFlags = CPU_ACCESS_WRITE;
    presentation_.device()->CreateBuffer(buffer, nullptr, &candidate.values);
    if (!candidate.values)
        throw std::runtime_error("Local reflection parameter allocation failed");
    GraphicsPipelineStateCreateInfo info;
    info.PSODesc.Name = "FORGE local color indirect and specular reflections";
    info.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC;
    SamplerDesc sampler;
    sampler.MinFilter = sampler.MagFilter = sampler.MipFilter = FILTER_TYPE_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = TEXTURE_ADDRESS_CLAMP;
    const ImmutableSamplerDesc immutable{SHADER_TYPE_PIXEL, "ProbeSampler", sampler};
    info.PSODesc.ResourceLayout.ImmutableSamplers = &immutable;
    info.PSODesc.ResourceLayout.NumImmutableSamplers = 1;
    auto& graphics = info.GraphicsPipeline;
    graphics.NumRenderTargets = specular_output ? 3 : 2;
    std::fill_n(graphics.RTVFormats, graphics.NumRenderTargets, TEX_FORMAT_RGBA16_FLOAT);
    graphics.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    graphics.RasterizerDesc.CullMode = CULL_MODE_NONE;
    graphics.DepthStencilDesc.DepthEnable = false;
    graphics.DepthStencilDesc.DepthWriteEnable = false;
    info.pVS = vertex;
    info.pPS = pixel;
    presentation_.graphics(info, &candidate.pipeline);
    if (!candidate.pipeline)
        throw std::runtime_error("Local reflection pipeline is unavailable");
    candidate.pipeline->CreateShaderResourceBinding(&candidate.binding, true);
    if (!candidate.binding ||
        !candidate.binding->GetVariableByName(SHADER_TYPE_PIXEL, "LocalReflectionValues"))
        throw std::runtime_error("Local reflection parameter interface is incomplete");
    for (const auto* name : surface_names)
        if (!candidate.binding->GetVariableByName(SHADER_TYPE_PIXEL, name))
            throw std::runtime_error("Local reflection surface interface is incomplete");
    for (const auto* name : cube_names)
        if (!candidate.binding->GetVariableByName(SHADER_TYPE_PIXEL, name))
            throw std::runtime_error("Local reflection cubemap interface is incomplete");
    candidate.binding->GetVariableByName(SHADER_TYPE_PIXEL, "LocalReflectionValues")
        ->Set(candidate.values);
    selected = std::move(candidate);
}
void SurfaceReflections::apply_probes(IDeviceContext* context, const LocalInput& input,
                                      ITextureView* color_output, ITextureView* indirect_output,
                                      ITextureView* specular_output) {
    if (thread_ != std::this_thread::get_id() || !context || input.probes.empty() ||
        input.probes.size() > probe_limit)
        throw std::invalid_argument(
            "Local reflections require their thread/context and 1 to 8 ready probes");
    if (!complete_surface_view(input.color, TEX_FORMAT_RGBA16_FLOAT, TEXTURE_VIEW_SHADER_RESOURCE))
        throw std::invalid_argument("Local reflections require a complete current HDR input");
    const auto& extent = input.color->GetTexture()->GetDesc();
    ITextureView* outputs[]{color_output, indirect_output, specular_output};
    const unsigned output_count = specular_output ? 3 : 2;
    const auto matching = [&](ITextureView* view, TEXTURE_FORMAT format, TEXTURE_VIEW_TYPE type) {
        return complete_surface_view(view, format, type) &&
               view->GetTexture()->GetDesc().Width == extent.Width &&
               view->GetTexture()->GetDesc().Height == extent.Height;
    };
    for (unsigned index = 0; index < output_count; ++index) {
        if (!matching(outputs[index], TEX_FORMAT_RGBA16_FLOAT, TEXTURE_VIEW_RENDER_TARGET))
            throw std::invalid_argument(
                "Local reflection results require matching complete HDR views");
        for (unsigned earlier = 0; earlier < index; ++earlier)
            if (outputs[earlier]->GetTexture() == outputs[index]->GetTexture())
                throw std::invalid_argument("Local reflection results must be distinct");
    }
    ITextureView* surfaces[]{input.color,   input.indirect, input.depth,
                             input.surface, input.response, input.specular};
    for (unsigned index = 0; index < std::size(surfaces); ++index) {
        if (!matching(surfaces[index], index == 2 ? TEX_FORMAT_D32_FLOAT : TEX_FORMAT_RGBA16_FLOAT,
                      TEXTURE_VIEW_SHADER_RESOURCE))
            throw std::invalid_argument(
                "Local reflection surface inputs require matching complete views");
        for (unsigned output = 0; output < output_count; ++output)
            if (surfaces[index]->GetTexture() == outputs[output]->GetTexture())
                throw std::invalid_argument("Local reflection results cannot alias surface inputs");
    }
    validate_surface_projection(input.projection);
    if (input.camera.projection != input.projection)
        throw std::invalid_argument("Local reflection camera differs from surface projection");
    std::array<float, header_floats + probe_floats * probe_limit> constants{};
    constants[0] = float(extent.Width);
    constants[1] = float(extent.Height);
    std::copy(input.projection.begin(), input.projection.end(), constants.begin() + 4);
    const Double3 basis[]{input.camera.right, input.camera.up, input.camera.forward};
    for (unsigned axis = 0; axis < 3; ++axis) {
        const auto& current = basis[axis];
        if (std::abs(std::hypot(current[0], current[1], current[2]) - 1) > 1e-6)
            throw std::invalid_argument("Local reflection camera requires unit basis vectors");
        for (unsigned earlier = 0; earlier < axis; ++earlier) {
            double dot = 0;
            for (unsigned coordinate = 0; coordinate < 3; ++coordinate)
                dot += current[coordinate] * basis[earlier][coordinate];
            if (std::abs(dot) > 1e-6)
                throw std::invalid_argument(
                    "Local reflection camera requires orthogonal basis vectors");
        }
        for (unsigned coordinate = 0; coordinate < 3; ++coordinate)
            constants[20 + 4 * axis + coordinate] = packed(basis[axis][coordinate]);
    }
    constants[32] = float(input.probes.size());
    for (unsigned index = 0; index < input.probes.size(); ++index) {
        const auto& probe = input.probes[index];
        if (!complete_probe_map(probe.specular) || !std::isfinite(probe.intensity) ||
            probe.intensity < 0 || probe.intensity > 1000000 ||
            !std::isfinite(probe.blend_distance) || probe.blend_distance < 0 ||
            probe.blend_distance > 1000000)
            throw std::invalid_argument("Local reflection map or lighting values are invalid");
        auto* values = constants.data() + header_floats + probe_floats * index;
        const auto center = reflection_probe_view_center(probe.volume, input.camera.position);
        std::copy(center.begin(), center.end(), values);
        values[3] = packed(probe.intensity);
        for (unsigned axis = 0; axis < 3; ++axis) {
            const auto& plane = probe.volume.planes[axis];
            if (std::abs(std::hypot(plane[0], plane[1], plane[2]) - 1) > 1e-6 ||
                plane[3] < std::numeric_limits<float>::min())
                throw std::invalid_argument("Local reflection planes require unit normals and "
                                            "positive world half distances");
            for (unsigned coordinate = 0; coordinate < 4; ++coordinate)
                values[4 + 4 * axis + coordinate] = packed(plane[coordinate]);
        }
        if (std::abs(std::hypot(probe.volume.map_rotation[0], probe.volume.map_rotation[1]) - 1) >
            1e-6)
            throw std::invalid_argument("Local reflection map rotation requires a unit direction");
        values[16] = packed(probe.blend_distance);
        values[17] = packed(probe.volume.map_rotation[0]);
        values[18] = packed(probe.volume.map_rotation[1]);
        values[19] = float(probe.specular->GetTexture()->GetDesc().MipLevels - 1);
        values[20] = probe.box_projection ? 1.f : 0.f;
    }
    prepare_probes(specular_output != nullptr);
    auto& selected = probes_[specular_output ? 1 : 0];
    {
        MapHelper<float> data(context, selected.values, MAP_WRITE, MAP_FLAG_DISCARD);
        float* mapped = data;
        std::copy(constants.begin(), constants.end(), mapped);
    }
    for (unsigned index = 0; index < std::size(surfaces); ++index)
        selected.binding->GetVariableByName(SHADER_TYPE_PIXEL, surface_names[index])
            ->Set(surfaces[index]);
    for (unsigned index = 0; index < probe_limit; ++index)
        selected.binding->GetVariableByName(SHADER_TYPE_PIXEL, cube_names[index])
            ->Set(input.probes[index < input.probes.size() ? index : 0].specular);
    context->SetRenderTargets(output_count, outputs, nullptr,
                              RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    Diligent::Viewport viewport{0, 0, float(extent.Width), float(extent.Height), 0, 1};
    context->SetViewports(1, &viewport, extent.Width, extent.Height);
    context->SetPipelineState(selected.pipeline);
    context->CommitShaderResources(selected.binding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->Draw(DrawAttribs{3, DRAW_FLAG_VERIFY_ALL});
    for (const auto* name : surface_names)
        selected.binding->GetVariableByName(SHADER_TYPE_PIXEL, name)->Set(nullptr);
    for (const auto* name : cube_names)
        selected.binding->GetVariableByName(SHADER_TYPE_PIXEL, name)->Set(nullptr);
}
} // namespace forge
