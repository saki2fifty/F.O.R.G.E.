#pragma once
#include "Utilities/interface/DiligentFXShaderSourceStreamFactory.hpp"
// Requires the native presentation test helpers from the same WARP fixture.
void check_punctual_lights(forge::DiligentPresentation& presentation,
                           Diligent::IDeviceContext* context) {
    using namespace Diligent;
    ShaderCreateInfo shader;
    shader.Desc.Name = "FORGE native punctual adapter proof";
    shader.Desc.ShaderType = SHADER_TYPE_COMPUTE;
    shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    shader.ShaderCompiler = SHADER_COMPILER_FXC;
    shader.HLSLVersion = {5, 1};
    shader.EntryPoint = "main";
    shader.pShaderSourceStreamFactory = &DiligentFXShaderSourceStreamFactory::GetInstance();
    shader.Source = R"(
#define USE_IBL 0
#include "ForgeLighting.fxh"
RWStructuredBuffer<float4> Destination;
cbuffer Fixture {float4 SurfacePosition;};
[numthreads(1,1,1)]
void main() {
    SurfaceShadingInfo s=(SurfaceShadingInfo)0;
    s.Pos=SurfacePosition.xyz;
    s.View=float3(0,0,-1);s.BaseLayer.Normal=s.View;s.BaseLayer.NdotV=1;
    s.BaseLayer.Srf=GetSurfaceReflectanceMR(float3(.7,.3,.1),.2,.6);
    PBRLightAttribs light=(PBRLightAttribs)0;
    light.Type=PBR_LIGHT_TYPE_POINT;light.PosX=.5;light.PosZ=-2;
    light.DirectionZ=1;light.ShadowMapIndex=-1;
    light.IntensityR=8;light.IntensityG=6;light.IntensityB=4;light.Range4=10000;
    light.SpotAngleScale=1/(.99-.9);light.SpotAngleOffset=-.9*light.SpotAngleScale;
    SurfaceLightingInfo reference=GetDefaultSurfaceLightingInfo();
    ApplyPunctualLight(s,light,reference);
    Destination[0]=float4(reference.Base.Punctual,1);
    SurfaceLightingInfo result=GetDefaultSurfaceLightingInfo();
    bool valid=ForgeApplyPunctualLight(s,light,result);
    Destination[1]=float4(result.Base.Punctual,valid?1:0);
    light.Type=PBR_LIGHT_TYPE_SPOT;result=GetDefaultSurfaceLightingInfo();
    valid=ForgeApplyPunctualLight(s,light,result);
    Destination[2]=float4(result.Base.Punctual,valid?1:0);
    reference=GetDefaultSurfaceLightingInfo();ApplyPunctualLight(s,light,reference);
    Destination[3]=float4(reference.Base.Punctual,1);
    light.PosX=0;light.PosZ=0;result.Base.Punctual=float3(1,2,3);
    valid=ForgeApplyPunctualLight(s,light,result);
    Destination[4]=float4(result.Base.Punctual,valid?1:0);
    light.PosX=2;light.PosZ=-2;result=GetDefaultSurfaceLightingInfo();
    valid=ForgeApplyPunctualLight(s,light,result);
    Destination[5]=float4(result.Base.Punctual,valid?1:0);
    light.Type=PBR_LIGHT_TYPE_DIRECTIONAL;light.DirectionZ=-1;
    result.Base.Punctual=float3(1,2,3);
    valid=ForgeApplyPunctualLight(s,light,result);
    Destination[6]=float4(result.Base.Punctual,valid?1:0);
    light.Type=PBR_LIGHT_TYPE_POINT;light.PosX=0;light.PosZ=-.01;
    light.IntensityR=light.IntensityG=light.IntensityB=3e38;
    valid=ForgeApplyPunctualLight(s,light,result);
    Destination[7]=float4(result.Base.Punctual,valid?1:0);
})";
    RefCntAutoPtr<IShader> compute;
    presentation.shader(shader, &compute);
    ComputePipelineStateCreateInfo pso;
    pso.PSODesc.Name = "FORGE punctual shader admission";
    pso.PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;
    pso.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    pso.pCS = compute;
    RefCntAutoPtr<IPipelineState> pipeline;
    presentation.compute(pso, &pipeline);
    BufferDesc desc;
    desc.Name = "FORGE punctual results";
    desc.Size = 8 * 4 * sizeof(float);
    desc.Mode = BUFFER_MODE_STRUCTURED;
    desc.ElementByteStride = 4 * sizeof(float);
    desc.BindFlags = BIND_UNORDERED_ACCESS;
    RefCntAutoPtr<IBuffer> output, staging;
    presentation.device()->CreateBuffer(desc, nullptr, &output);
    desc.Name = "FORGE punctual readback";
    desc.Mode = BUFFER_MODE_UNDEFINED;
    desc.ElementByteStride = 0;
    desc.BindFlags = BIND_NONE;
    desc.Usage = USAGE_STAGING;
    desc.CPUAccessFlags = CPU_ACCESS_READ;
    presentation.device()->CreateBuffer(desc, nullptr, &staging);
    require(output && staging, "Punctual light buffers unavailable");
    RefCntAutoPtr<IShaderResourceBinding> bindings;
    pipeline->CreateShaderResourceBinding(&bindings, true);
    BufferDesc seed_desc;
    seed_desc.Name = "FORGE punctual test surface";
    seed_desc.Size = 16;
    seed_desc.BindFlags = BIND_UNIFORM_BUFFER;
    seed_desc.Usage = USAGE_IMMUTABLE;
    const float position[4]{};
    BufferData initial{position, sizeof(position)};
    RefCntAutoPtr<IBuffer> seed;
    presentation.device()->CreateBuffer(seed_desc, &initial, &seed);
    auto* fixture = bindings->GetVariableByName(SHADER_TYPE_COMPUTE, "Fixture");
    require(seed && fixture, "Punctual fixture uniform binding absent");
    fixture->Set(seed);
    auto* target = bindings->GetVariableByName(SHADER_TYPE_COMPUTE, "Destination");
    require(target != nullptr, "Punctual light output binding absent");
    target->Set(output->GetDefaultView(BUFFER_VIEW_UNORDERED_ACCESS));
    context->SetPipelineState(pipeline);
    context->CommitShaderResources(bindings, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->DispatchCompute(DispatchComputeAttribs{1, 1, 1});
    context->CopyBuffer(output, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, staging, 0, desc.Size,
                        RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->WaitForIdle();
    void* mapped = nullptr;
    context->MapBuffer(staging, MAP_READ, MAP_FLAG_DO_NOT_WAIT, mapped);
    require(mapped != nullptr, "Punctual light readback unavailable");
    std::array<std::array<float, 4>, 8> result;
    std::memcpy(result.data(), mapped, sizeof(result));
    context->UnmapBuffer(staging, MAP_READ);
    context->FinishFrame();
    const double cone = std::clamp((2 / std::sqrt(4.25) - .9) / (.99 - .9), 0., 1.);
    for (const auto& row : result)
        for (const auto value : row)
            require(std::isfinite(value), "Punctual adapter published NaN/Inf");
    require(result[0][0] > 0 && result[1][3] == 1 && result[2][3] == 1,
            "Punctual adapter lost valid lighting");
    double upstream_difference = 0;
    for (unsigned c = 0; c < 3; ++c) {
        require(std::abs(result[1][c] - result[0][c]) < 1e-5,
                "Point adapter differs from native BRDF");
        require(std::abs(result[2][c] - result[0][c] * cone * cone) < 1e-5,
                "Spot adapter has incorrect position direction or squared cone attenuation");
        upstream_difference += std::abs(result[3][c] - result[2][c]);
        require(result[5][c] == 0, "Outside-cone surface is lit");
    }
    require(upstream_difference > 1e-4, "Pinned spotlight discrepancy reproduction changed");
    for (unsigned i : {4u, 6u, 7u})
        require(result[i] == std::array<float, 4>{1, 2, 3, 0},
                "Undefined punctual contribution corrupted previous lighting");
}
