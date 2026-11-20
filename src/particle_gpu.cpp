#include "particle_gpu.hpp"
#include "Graphics/GraphicsEngine/interface/BufferView.h"
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include "mesh_instance_layout.hpp"
#include "particle_gpu_shaders.hpp"
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>

namespace forge {
using namespace Diligent;
ParticleGpuInstances::ParticleGpuInstances(DiligentPresentation& presentation, unsigned capacity)
    : presentation_(presentation), capacity_(capacity) {
    if (capacity < 1 || capacity > 1024)
        throw std::runtime_error("GPU particle capacity must be in [1,1024]");
}
ParticleGpuInstances::ParticleGpuInstances(DiligentPresentation& presentation, unsigned capacity,
                                           IShader* graph, const ParticleGpuGraphLimits& limits)
    : ParticleGpuInstances(presentation, capacity) {
    if (!graph)
        throw std::runtime_error("Particle graph requires a realized Compute program");
    validate_particle_gpu_graph_limits(limits);
    graph_program_ = graph;
    graph_limits_ = limits;
}
bool ParticleGpuInstances::supported() const {
    return presentation_.device()->GetDeviceInfo().Features.ComputeShaders ==
           DEVICE_FEATURE_STATE_ENABLED;
}
void ParticleGpuInstances::prepare() {
    if (pipeline_)
        return;
    const auto rows = std::to_string(mesh_static_instance_rows);
    const ShaderMacro macros[]{{"FORGE_STATIC_INSTANCE_ROWS", rows.c_str()}};
    ShaderCreateInfo shader;
    shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    shader.EntryPoint = "main";
    shader.Desc.Name = "FORGE GPU analytic particle instances";
    shader.Desc.ShaderType = SHADER_TYPE_COMPUTE;
    shader.Source = particle_gpu_compute;
    shader.Macros = {macros, 1};
    RefCntAutoPtr<IShader> native = graph_program_;
    if (!native)
        presentation_.shader(shader, &native);
    if (!native)
        throw std::runtime_error("GPU particle compute shader unavailable");
    ComputePipelineStateCreateInfo info;
    info.PSODesc.Name = "FORGE GPU analytic particle instances";
    info.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    info.pCS = native;
    RefCntAutoPtr<IPipelineState> pipeline;
    presentation_.compute(info, &pipeline);
    if (!pipeline)
        throw std::runtime_error("GPU particle compute pipeline unavailable");
    RefCntAutoPtr<IShaderResourceBinding> binding;
    pipeline->CreateShaderResourceBinding(&binding, true);
    if (!binding || !binding->GetVariableByName(SHADER_TYPE_COMPUTE, "ParticleValues") ||
        !binding->GetVariableByName(SHADER_TYPE_COMPUTE, "ParticleRows"))
        throw std::runtime_error("GPU particle compute binding unavailable");
    BufferDesc input;
    input.Name = "FORGE GPU particle values";
    input.Size = sizeof(ParticleGpuValues);
    input.Usage = USAGE_DYNAMIC;
    input.BindFlags = BIND_UNIFORM_BUFFER;
    input.CPUAccessFlags = CPU_ACCESS_WRITE;
    RefCntAutoPtr<IBuffer> values;
    presentation_.device()->CreateBuffer(input, nullptr, &values);
    BufferDesc output;
    output.Name = "FORGE GPU particle instance rows";
    output.Size = capacity_ * mesh_static_instance_rows * sizeof(MeshInstanceRow);
    output.Mode = BUFFER_MODE_STRUCTURED;
    output.ElementByteStride = sizeof(MeshInstanceRow);
    output.BindFlags = BIND_VERTEX_BUFFER | BIND_UNORDERED_ACCESS;
    RefCntAutoPtr<IBuffer> instances;
    presentation_.device()->CreateBuffer(output, nullptr, &instances);
    if (!values || !instances || !instances->GetDefaultView(BUFFER_VIEW_UNORDERED_ACCESS))
        throw std::runtime_error("GPU particle buffer allocation unavailable");
    binding->GetVariableByName(SHADER_TYPE_COMPUTE, "ParticleValues")->Set(values);
    binding->GetVariableByName(SHADER_TYPE_COMPUTE, "ParticleRows")
        ->Set(instances->GetDefaultView(BUFFER_VIEW_UNORDERED_ACCESS));
    if (graph_program_) {
        auto* variable = binding->GetVariableByName(SHADER_TYPE_COMPUTE, "ParticleGraphLimits");
        if (!variable)
            throw std::runtime_error("Particle graph limits binding unavailable");
        BufferDesc graph_desc;
        graph_desc.Name = "FORGE particle graph local bounds";
        graph_desc.Size = sizeof(ParticleGpuGraphLimits);
        graph_desc.Usage = USAGE_IMMUTABLE;
        graph_desc.BindFlags = BIND_UNIFORM_BUFFER;
        BufferData graph_data{&graph_limits_, sizeof(graph_limits_)};
        RefCntAutoPtr<IBuffer> graph_values;
        presentation_.device()->CreateBuffer(graph_desc, &graph_data, &graph_values);
        if (!graph_values)
            throw std::runtime_error("Particle graph limits buffer unavailable");
        variable->Set(graph_values);
        graph_values_ = std::move(graph_values);
    }
    pipeline_ = std::move(pipeline);
    binding_ = std::move(binding);
    values_ = std::move(values);
    output_ = std::move(instances);
}
void ParticleGpuInstances::prepare_billboards() {
    if (billboard_pipeline_)
        return;
    const auto rows = std::to_string(mesh_static_instance_rows);
    const ShaderMacro macros[]{{"FORGE_STATIC_INSTANCE_ROWS", rows.c_str()}};
    ShaderCreateInfo shader;
    shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    shader.EntryPoint = "main";
    shader.Desc.Name = "FORGE GPU particle camera projection";
    shader.Desc.ShaderType = SHADER_TYPE_COMPUTE;
    shader.Source = particle_gpu_billboard_compute;
    shader.Macros = {macros, 1};
    RefCntAutoPtr<IShader> native;
    presentation_.shader(shader, &native);
    if (!native)
        throw std::runtime_error("GPU particle camera shader unavailable");
    ComputePipelineStateCreateInfo info;
    info.PSODesc.Name = "FORGE GPU particle camera projection";
    info.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    info.pCS = native;
    RefCntAutoPtr<IPipelineState> pipeline;
    presentation_.compute(info, &pipeline);
    if (!pipeline)
        throw std::runtime_error("GPU particle camera pipeline unavailable");
    RefCntAutoPtr<IShaderResourceBinding> binding;
    pipeline->CreateShaderResourceBinding(&binding, true);
    if (!binding || !binding->GetVariableByName(SHADER_TYPE_COMPUTE, "ParticleBillboardValues") ||
        !binding->GetVariableByName(SHADER_TYPE_COMPUTE, "ParticleRows"))
        throw std::runtime_error("GPU particle camera binding unavailable");
    BufferDesc input;
    input.Name = "FORGE GPU particle camera values";
    input.Size = sizeof(ParticleGpuBillboardValues);
    input.Usage = USAGE_DYNAMIC;
    input.BindFlags = BIND_UNIFORM_BUFFER;
    input.CPUAccessFlags = CPU_ACCESS_WRITE;
    RefCntAutoPtr<IBuffer> values;
    presentation_.device()->CreateBuffer(input, nullptr, &values);
    if (!values)
        throw std::runtime_error("GPU particle camera buffer unavailable");
    binding->GetVariableByName(SHADER_TYPE_COMPUTE, "ParticleBillboardValues")->Set(values);
    binding->GetVariableByName(SHADER_TYPE_COMPUTE, "ParticleRows")
        ->Set(output_->GetDefaultView(BUFFER_VIEW_UNORDERED_ACCESS));
    billboard_pipeline_ = std::move(pipeline);
    billboard_binding_ = std::move(binding);
    billboard_values_ = std::move(values);
}
void ParticleGpuInstances::evaluate(IDeviceContext* context, const ParticleGpuValues& values,
                                    const CameraView* billboard_view, bool material_tint_only,
                                    const ParticleGpuOpacityValues* opacity) {
    count_ = 0;
    if (!context || !supported())
        throw std::runtime_error("GPU particle evaluation requires enabled compute support");
    if (values.births[0] > capacity_ || values.modes[0] > 3 || values.modes[1] > 16 ||
        values.modes[2] > 16 || values.modes[1] == 1 || values.modes[2] == 1)
        throw std::runtime_error("GPU particle input exceeds its bounded compute profile");
    if (graph_program_)
        validate_particle_gpu_graph_envelope(values, graph_limits_);
    std::optional<ParticleGpuBillboardValues> alignment;
    if (billboard_view)
        alignment = prepare_particle_gpu_billboard_values(*billboard_view, values.births[0]);
    else if (material_tint_only || opacity) {
        alignment.emplace();
        alignment->counts[0] = values.births[0];
    }
    if (opacity) {
        validate_particle_gpu_opacity_values(*opacity);
        if (!std::isfinite(values.timing[0]) || values.timing[0] < 0 ||
            !std::isfinite(values.timing[1]) || values.timing[1] < 0 ||
            !std::isfinite(values.timing[2]) || values.timing[2] <= 0)
            throw std::runtime_error(
                "GPU particle opacity requires finite short ages and lifetime");
        alignment->counts[2] = opacity->count;
        alignment->counts[3] = 1;
        alignment->timing = values.timing;
        alignment->opacity = {opacity->endpoints[0], opacity->endpoints[1], 0, 0};
        alignment->opacity_stops = opacity->stops;
    }
    if (!values.births[0])
        return;
    prepare();
    if (alignment) {
        prepare_billboards();
        MapHelper<ParticleGpuBillboardValues> mapped(context, billboard_values_, MAP_WRITE,
                                                     MAP_FLAG_DISCARD);
        if (!static_cast<ParticleGpuBillboardValues*>(mapped))
            throw std::runtime_error("GPU particle camera upload unavailable");
        mapped[0] = *alignment;
    }
    {
        MapHelper<ParticleGpuValues> mapped(context, values_, MAP_WRITE, MAP_FLAG_DISCARD);
        if (!static_cast<ParticleGpuValues*>(mapped))
            throw std::runtime_error("GPU particle parameter upload unavailable");
        mapped[0] = values;
    }
    context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
    context->SetPipelineState(pipeline_);
    context->CommitShaderResources(binding_, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->DispatchCompute(DispatchComputeAttribs{(values.births[0] + 63) / 64, 1, 1});
    if (alignment) {
        StateTransitionDesc barrier(output_, RESOURCE_STATE_UNORDERED_ACCESS,
                                    RESOURCE_STATE_UNORDERED_ACCESS);
        context->TransitionResourceStates(1, &barrier);
        context->SetPipelineState(billboard_pipeline_);
        context->CommitShaderResources(billboard_binding_,
                                       RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->DispatchCompute(DispatchComputeAttribs{(values.births[0] + 63) / 64, 1, 1});
    }
    count_ = values.births[0];
}
} // namespace forge
