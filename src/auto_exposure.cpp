#include "auto_exposure.hpp"
#include "Graphics/GraphicsEngine/interface/BufferView.h"
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include "auto_exposure_shaders.hpp"
#include "display_source.hpp"
#include <string>
namespace forge {
using namespace Diligent;
namespace {
struct alignas(16) Parameters {
    std::array<std::uint32_t, 4> size;
    std::array<float, 4> limits, adaptation;
    std::array<std::uint32_t, 4> history;
};
static_assert(sizeof(Parameters) == 64);
static_assert(auto_exposure_bins == auto_exposure_tile_x * auto_exposure_tile_y);
} // namespace
void AutoExposure::prepare() {
    if (values_)
        return;
    const auto definitions =
        "#define FORGE_AUTO_BINS " + std::to_string(auto_exposure_bins) + "\n" +
        "#define FORGE_AUTO_TILE_X " + std::to_string(auto_exposure_tile_x) + "\n" +
        "#define FORGE_AUTO_TILE_Y " + std::to_string(auto_exposure_tile_y) + "\n" +
        "#define FORGE_AUTO_LOG_MIN (" + std::to_string(auto_exposure_log_minimum) + ")\n" +
        "#define FORGE_AUTO_LOG_MAX (" + std::to_string(auto_exposure_log_maximum) + ")\n";
    std::array<RefCntAutoPtr<IPipelineState>, 2> pipelines;
    std::array<RefCntAutoPtr<IShaderResourceBinding>, 2> bindings;
    for (unsigned i = 0; i < 2; ++i) {
        const auto source =
            definitions + auto_exposure_shader::common +
            (i == 0 ? auto_exposure_shader::histogram : auto_exposure_shader::adaptation);
        ShaderCreateInfo shader;
        shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
        shader.EntryPoint = "main";
        shader.Desc.ShaderType = SHADER_TYPE_COMPUTE;
        shader.Desc.Name =
            i == 0 ? "FORGE bounded exposure histogram" : "FORGE exposure adaptation";
        shader.Source = source.c_str();
        RefCntAutoPtr<IShader> native;
        presentation_.shader(shader, &native); // Existing backend capability/compiler admission.
        ComputePipelineStateCreateInfo info;
        info.PSODesc.Name = shader.Desc.Name;
        info.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
        const ShaderResourceVariableDesc dynamic{SHADER_TYPE_COMPUTE, "g_Source",
                                                 SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC};
        if (i == 0) {
            info.PSODesc.ResourceLayout.Variables = &dynamic;
            info.PSODesc.ResourceLayout.NumVariables = 1;
        }
        info.pCS = native;
        presentation_.compute(info, &pipelines[i]);
        pipelines[i]->CreateShaderResourceBinding(&bindings[i], true);
        if (!bindings[i] ||
            !bindings[i]->GetVariableByName(SHADER_TYPE_COMPUTE, "ForgeAutoExposure") ||
            !bindings[i]->GetVariableByName(SHADER_TYPE_COMPUTE, "g_TileHistogram") ||
            !bindings[i]->GetVariableByName(SHADER_TYPE_COMPUTE,
                                            i == 0 ? "g_Source" : "g_Exposure"))
            throw std::runtime_error("Automatic exposure shader interface is incomplete");
    }
    RefCntAutoPtr<IBuffer> histogram, exposure, values;
    BufferDesc desc;
    desc.Name = "FORGE bounded exposure tile histograms";
    desc.Size = auto_exposure_histogram_bytes;
    desc.Mode = BUFFER_MODE_STRUCTURED;
    desc.ElementByteStride = sizeof(std::uint32_t);
    desc.BindFlags = BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS;
    device_->CreateBuffer(desc, nullptr, &histogram);
    desc.Name = "FORGE exposure history";
    desc.Size = desc.ElementByteStride = 16;
    const std::array<float, 4> initial{};
    const BufferData initial_data{initial.data(), sizeof(initial)};
    device_->CreateBuffer(desc, &initial_data, &exposure);
    desc = {};
    desc.Name = "FORGE exposure controls";
    desc.Size = sizeof(Parameters);
    desc.BindFlags = BIND_UNIFORM_BUFFER;
    desc.Usage = USAGE_DYNAMIC;
    desc.CPUAccessFlags = CPU_ACCESS_WRITE;
    device_->CreateBuffer(desc, nullptr, &values);
    if (!histogram || !exposure || !values ||
        !histogram->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE) ||
        !histogram->GetDefaultView(BUFFER_VIEW_UNORDERED_ACCESS) ||
        !exposure->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE) ||
        !exposure->GetDefaultView(BUFFER_VIEW_UNORDERED_ACCESS))
        throw std::runtime_error("Automatic exposure buffer allocation failed");
    for (auto& binding : bindings)
        binding->GetVariableByName(SHADER_TYPE_COMPUTE, "ForgeAutoExposure")->Set(values);
    bindings[0]
        ->GetVariableByName(SHADER_TYPE_COMPUTE, "g_TileHistogram")
        ->Set(histogram->GetDefaultView(BUFFER_VIEW_UNORDERED_ACCESS));
    bindings[1]
        ->GetVariableByName(SHADER_TYPE_COMPUTE, "g_TileHistogram")
        ->Set(histogram->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));
    bindings[1]
        ->GetVariableByName(SHADER_TYPE_COMPUTE, "g_Exposure")
        ->Set(exposure->GetDefaultView(BUFFER_VIEW_UNORDERED_ACCESS));
    pipelines_ = std::move(pipelines);
    bindings_ = std::move(bindings);
    histogram_ = std::move(histogram);
    exposure_ = std::move(exposure);
    values_ = std::move(values);
}
IBufferView* AutoExposure::apply(IDeviceContext* context, ITextureView* source,
                                 const SceneAutoExposure& settings, float delta, bool reset) {
    validate_scene_auto_exposure(settings);
    if (!context || context->GetDesc().IsDeferred || !std::isfinite(delta) || delta < 0 ||
        delta > .25f)
        throw std::runtime_error(
            "Automatic exposure requires its immediate context and bounded elapsed time");
    const auto extent = display_source_extent(source);
    const auto plan = auto_exposure_plan(extent[0], extent[1],
                                         device_->GetAdapterInfo().Texture.MaxTexture2DDimension);
    if (!settings.enabled)
        return nullptr;
    prepare();
    {
        MapHelper<Parameters> values(context, values_, MAP_WRITE, MAP_FLAG_DISCARD);
        if (!static_cast<Parameters*>(values))
            throw std::runtime_error("Automatic exposure parameter map failed");
        values[0] = {
            {plan.width, plan.height, plan.sample_width, plan.sample_height},
            {settings.min_ev, settings.max_ev, settings.low_percentile, settings.high_percentile},
            {settings.target_gray, settings.speed_up, settings.speed_down, delta},
            {reset ? 1u : 0u, plan.groups_x, plan.groups_y, 0}};
    }
    context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
    bindings_[0]->GetVariableByName(SHADER_TYPE_COMPUTE, "g_Source")->Set(source);
    context->SetPipelineState(pipelines_[0]);
    context->CommitShaderResources(bindings_[0], RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    DispatchComputeAttribs histogram;
    histogram.ThreadGroupCountX = plan.groups_x;
    histogram.ThreadGroupCountY = plan.groups_y;
    histogram.MtlThreadGroupSizeX = auto_exposure_tile_x;
    histogram.MtlThreadGroupSizeY = auto_exposure_tile_y;
    histogram.MtlThreadGroupSizeZ = 1;
    context->DispatchCompute(histogram);
    context->SetPipelineState(pipelines_[1]);
    // UAV->SRV transition orders the completed per-tile writes. No global UAV
    // clear or cross-group atomic update is required before the reduction.
    context->CommitShaderResources(bindings_[1], RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    DispatchComputeAttribs adaptation;
    adaptation.MtlThreadGroupSizeX = auto_exposure_bins;
    adaptation.MtlThreadGroupSizeY = adaptation.MtlThreadGroupSizeZ = 1;
    context->DispatchCompute(adaptation);
    return exposure_->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE);
}
void AutoExposure::clear_source() {
    if (bindings_[0])
        bindings_[0]->GetVariableByName(SHADER_TYPE_COMPUTE, "g_Source")->Set(nullptr);
}
} // namespace forge
