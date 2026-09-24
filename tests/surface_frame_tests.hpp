#pragma once
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include "Utilities/interface/DiligentFXShaderSourceStreamFactory.hpp"
#include <forge/transform.hpp>
void check_surface_frames(forge::DiligentPresentation& presentation,
                          Diligent::IDeviceContext* context) {
    using namespace Diligent;
    struct Input {
        std::array<float, 4> row0, row1, row2, normal, tangent;
    };
    struct Output {
        std::array<float, 4> normal, tangent, bitangent, mapped, flags, special;
    };
    static_assert(sizeof(Input) == 80 && sizeof(Output) == 96);
    std::vector<Input> inputs;
    std::vector<forge::AffineTransform> worlds;
    for (const auto scale : {std::array<float, 3>{1, 1, 1},
                             {-1, 1, 1},
                             {-1, -2, 3},
                             {1, 1, 0},
                             {-1, 1, 0},
                             {0, 1, 0},
                             {0, 0, 0},
                             {1e-20f, 1e-20f, 1e-20f},
                             {1e20f, 1e20f, 1e20f}}) {
        forge::AffineTransform world;
        world.m[0] = scale[0];
        world.m[5] = scale[1];
        world.m[10] = scale[2];
        worlds.push_back(world);
    }
    // A sheared nonuniform reflected transform, possible after spatial ancestry.
    forge::AffineTransform shear;
    shear.m = {-2, .3, 0, 0, 0, 3, .2, 0, 0, 0, .5, 0};
    worlds.push_back(shear);
    for (const auto& world : worlds)
        for (float handedness : {-1.f, 1.f}) {
            Input item;
            for (unsigned axis = 0; axis < 4; ++axis) {
                item.row0[axis] = float(world.m[axis]);
                item.row1[axis] = float(world.m[4 + axis]);
                item.row2[axis] = float(world.m[8 + axis]);
            }
            item.normal = {0, 0, 1, 0};
            item.tangent = {1, 0, 0, handedness};
            inputs.push_back(item);
        }
    auto* device = presentation.device();
    BufferDesc desc;
    desc.Name = "FORGE surface-frame input";
    desc.Size = inputs.size() * sizeof(Input);
    desc.Mode = BUFFER_MODE_STRUCTURED;
    desc.ElementByteStride = sizeof(Input);
    desc.BindFlags = BIND_SHADER_RESOURCE;
    desc.Usage = USAGE_IMMUTABLE;
    BufferData initial{inputs.data(), desc.Size};
    RefCntAutoPtr<IBuffer> input, output, staging;
    device->CreateBuffer(desc, &initial, &input);
    desc.Name = "FORGE surface-frame output";
    desc.BindFlags = BIND_UNORDERED_ACCESS;
    desc.Usage = USAGE_DEFAULT;
    desc.Size = inputs.size() * sizeof(Output);
    desc.ElementByteStride = sizeof(Output);
    device->CreateBuffer(desc, nullptr, &output);
    require(input && output, "Surface-frame buffers unavailable");
    desc.Name = "FORGE surface-frame staging";
    desc.Mode = BUFFER_MODE_UNDEFINED;
    desc.ElementByteStride = 0;
    desc.BindFlags = BIND_NONE;
    desc.Usage = USAGE_STAGING;
    desc.CPUAccessFlags = CPU_ACCESS_READ;
    device->CreateBuffer(desc, nullptr, &staging);
    require(bool(staging), "Surface-frame staging unavailable");
    ShaderCreateInfo shader;
    shader.Desc.Name = "FORGE signed surface-frame acceptance";
    shader.Desc.ShaderType = SHADER_TYPE_COMPUTE;
    shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    shader.ShaderCompiler = SHADER_COMPILER_FXC;
    shader.HLSLVersion = {5, 1};
    shader.EntryPoint = "main";
    shader.pShaderSourceStreamFactory = &DiligentFXShaderSourceStreamFactory::GetInstance();
    shader.Source = R"(
#include "ForgeSurface.fxh"
struct Input {float4 Row0, Row1, Row2, Normal, Tangent;};
struct Output {float4 Normal, Tangent, Bitangent, Mapped, Flags, Special;};
StructuredBuffer<Input> Source;
RWStructuredBuffer<Output> Destination;
[numthreads(1,1,1)]
void main(uint id:SV_DispatchThreadID) {
    Input value=Source[id];
    float3x3 basis=float3x3(value.Row0.xyz,value.Row1.xyz,value.Row2.xyz);
    ForgeSurfaceFrame f=ForgeMakeSurfaceFrame(basis,value.Normal.xyz,value.Tangent);
    Output o;
    o.Normal=float4(f.Normal,0);o.Tangent=float4(f.Tangent,0);o.Bitangent=float4(f.Bitangent,0);
    o.Mapped=float4(ForgePerturbNormal(f,float3(.3,.4,.8660254)),0);
    ForgeSurfaceFrame zero=ForgeMakeSurfaceFrame((float3x3)0,float3(0,1,0),float4(1,0,0,1));
    o.Flags=float4(f.NormalValid?1:0,f.TangentValid?1:0,
        dot(ForgeUnit(float3(0,0,0)),float3(1,1,1)),
        dot(abs(zero.Normal)+abs(zero.Tangent)+abs(zero.Bitangent),float3(1,1,1)));
    float3 rejected = ForgeUnit(float3(asfloat(0x7f800000u),1,0)) +
                      ForgeUnit(float3(0,asfloat(0xff800000u),1)) +
                      ForgeUnit(float3(1,0,asfloat(0x7fc00001u)));
    bool classified = ForgeFinite(0) && ForgeFinite(asfloat(0x80000000u)) &&
        ForgeFinite(asfloat(0x7f7fffffu)) && ForgeFinite(asfloat(1u)) &&
        !ForgeFinite(asfloat(0x7f800000u)) && !ForgeFinite(asfloat(0xff800000u)) &&
        !ForgeFinite(asfloat(0x7fc00001u));
    o.Special=float4(rejected,classified?1:0);
    Destination[id]=o;
})";
    RefCntAutoPtr<IShader> compute;
    presentation.shader(shader, &compute);
    ComputePipelineStateCreateInfo pso;
    pso.PSODesc.Name = "FORGE surface-frame proof";
    pso.PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;
    pso.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    pso.pCS = compute;
    RefCntAutoPtr<IPipelineState> pipeline;
    presentation.compute(pso, &pipeline);
    RefCntAutoPtr<IShaderResourceBinding> bindings;
    pipeline->CreateShaderResourceBinding(&bindings, true);
    auto* source = bindings->GetVariableByName(SHADER_TYPE_COMPUTE, "Source");
    auto* destination = bindings->GetVariableByName(SHADER_TYPE_COMPUTE, "Destination");
    require(source && destination, "Surface-frame shader bindings absent");
    source->Set(input->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));
    destination->Set(output->GetDefaultView(BUFFER_VIEW_UNORDERED_ACCESS));
    context->SetPipelineState(pipeline);
    context->CommitShaderResources(bindings, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    DispatchComputeAttribs dispatch;
    dispatch.ThreadGroupCountX = Uint32(inputs.size());
    context->DispatchCompute(dispatch);
    context->CopyBuffer(output, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, staging, 0, desc.Size,
                        RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->WaitForIdle();
    void* mapped = nullptr;
    context->MapBuffer(staging, MAP_READ, MAP_FLAG_DO_NOT_WAIT, mapped);
    require(mapped != nullptr, "Surface-frame results unavailable");
    std::vector<Output> results(inputs.size());
    std::memcpy(results.data(), mapped, results.size() * sizeof(Output));
    context->UnmapBuffer(staging, MAP_READ);
    context->FinishFrame();
    for (std::size_t i = 0; i < results.size(); ++i) {
        const auto& value = results[i];
        for (const auto* v : {&value.normal, &value.tangent, &value.bitangent, &value.mapped,
                              &value.flags, &value.special})
            for (const auto scalar : *v)
                require(std::isfinite(scalar),
                        "Signed/singular GPU surface frame generated NaN/Inf");
        require(value.flags[2] == 0 && value.flags[3] == 0,
                "Constant zero normal/basis did not produce a finite zero frame");
        require(value.special == std::array<float, 4>{0, 0, 0, 1},
                "GPU finite classification or nonfinite normalization rejection failed");
        const auto& world = worlds[i / 2];
        const auto normals = forge::normal_transform(world);
        forge::Double3 expected{normals.m[2], normals.m[6], normals.m[10]};
        const auto length = std::hypot(expected[0], expected[1], expected[2]);
        require(value.flags[0] == (length > 0 ? 1.f : 0.f),
                "GPU normal degeneracy differs from CPU");
        if (length > 0) {
            for (unsigned a = 0; a < 3; ++a)
                require(std::abs(value.normal[a] - expected[a] / length) < 3e-5,
                        "GPU normal/cofactor orientation differs from robust CPU transform");
            require(value.flags[1] == 1, "Useful planar/mirrored tangent frame was lost");
            const double handedness = inputs[i].tangent[3];
            const double bitangent_dot = value.bitangent[0] * world.m[1] * handedness +
                                         value.bitangent[1] * world.m[5] * handedness +
                                         value.bitangent[2] * world.m[9] * handedness;
            require(bitangent_dot > 0, "GPU tangent frame lost source UV/reflection handedness");
            require(std::abs(std::hypot(value.mapped[0], value.mapped[1], value.mapped[2]) - 1) <
                        3e-5,
                    "Normal-mapped signed surface is not normalized");
        } else
            require(value.flags[1] == 0, "Collapsed geometry invented a tangent frame");
    }
}
