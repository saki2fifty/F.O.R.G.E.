#include "display_resolve.hpp"
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include "Utilities/interface/DiligentFXShaderSourceStreamFactory.hpp"
#include "auto_exposure.hpp"
#include "display_source.hpp"
#include "fast_approximate_aa_shader.hpp"
#include "fullscreen_triangle_shader.hpp"
#include "morphological_aa_shader.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
namespace forge {
using namespace Diligent;
namespace {
using Pipeline = std::pair<RefCntAutoPtr<IPipelineState>, RefCntAutoPtr<IShaderResourceBinding>>;
Pipeline display_pipeline(DiligentPresentation& presentation, bool automatic) {
    ShaderCreateInfo shader;
    shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    shader.EntryPoint = "main";
    shader.pShaderSourceStreamFactory = &DiligentFXShaderSourceStreamFactory::GetInstance();
    shader.Desc.Name = "FORGE display fullscreen triangle";
    shader.Desc.ShaderType = SHADER_TYPE_VERTEX;
    shader.Source = fullscreen_triangle_vertex;
    RefCntAutoPtr<IShader> vertex, pixel;
    presentation.shader(shader, &vertex);
    shader.Desc.Name = automatic ? "FORGE automatic PBR Neutral display resolve"
                                 : "FORGE PBR Neutral display resolve";
    shader.Desc.ShaderType = SHADER_TYPE_PIXEL;
    std::string source = R"(
#define TONE_MAPPING_MODE TONE_MAPPING_MODE_PBR_NEUTRAL
#include "ToneMapping.fxh"
Texture2D<float4> g_HDR;
)";
    if (automatic)
        source += "StructuredBuffer<float4> g_AutoExposure;\n";
    source += R"(
cbuffer ForgeDisplay {float4 g_Display;float4 g_Filter;};
float4 main(float4 position:SV_Position):SV_Target0 {
    float4 source=g_HDR.Load(int3(int2(position.xy),0));
    float3 exposed=source.rgb*g_Display.x;
)";
    if (automatic)
        source += "exposed*=exp2(g_AutoExposure[0].x);\n";
    source += R"(
    if(g_Display.y!=0)exposed*=g_Filter.rgb;
    if(!all(isfinite(source))||!all(isfinite(exposed)))return float4(1,0,1,1);
    ToneMappingAttribs settings=(ToneMappingAttribs)0;
    settings.fMiddleGray=.18;settings.fWhitePoint=3;settings.fLuminanceSaturation=1;
    // PBR Neutral uses .3/average luminance. Exposure is applied above;
    // .3 therefore leaves the selected gain unchanged.
    float3 mapped=ToneMap(exposed,settings,.3);
    if(!all(isfinite(mapped)))return float4(1,0,1,1);
    if(g_Display.y!=0) {
        float luminance=dot(mapped,float3(.2126,.7152,.0722));
        mapped=lerp(luminance.xxx,mapped,g_Display.z);
        mapped=saturate((mapped-.18)*g_Display.w+.18);
    }
    return float4(LinearToSRGB(mapped),saturate(source.a));
})";
    shader.Source = source.c_str();
    presentation.shader(shader, &pixel);
    GraphicsPipelineStateCreateInfo ci;
    ci.PSODesc.Name = automatic ? "FORGE automatic HDR to display" : "FORGE HDR to display";
    ci.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    const ShaderResourceVariableDesc inputs[]{
        {SHADER_TYPE_PIXEL, "g_HDR", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
        {SHADER_TYPE_PIXEL, "g_AutoExposure", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC}};
    ci.PSODesc.ResourceLayout.Variables = inputs;
    ci.PSODesc.ResourceLayout.NumVariables = automatic ? 2 : 1;

    ci.GraphicsPipeline.NumRenderTargets = 1;
    ci.GraphicsPipeline.RTVFormats[0] = TEX_FORMAT_RGBA8_UNORM;
    ci.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    ci.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_NONE;
    ci.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;
    ci.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = false;
    ci.pVS = vertex;
    ci.pPS = pixel;
    Pipeline result;
    presentation.graphics(ci, &result.first);
    result.first->CreateShaderResourceBinding(&result.second, true);
    if (!result.second || !result.second->GetVariableByName(SHADER_TYPE_PIXEL, "ForgeDisplay") ||
        !result.second->GetVariableByName(SHADER_TYPE_PIXEL, "g_HDR") ||
        (automatic && !result.second->GetVariableByName(SHADER_TYPE_PIXEL, "g_AutoExposure")))
        throw std::runtime_error("Display resolve shader interface is incomplete");
    return result;
}
} // namespace
DisplayResolve::DisplayResolve(DiligentPresentation& presentation)
    : presentation_(presentation), device_(presentation.device()) {
    auto pipeline = display_pipeline(presentation, false);
    pipeline_ = std::move(pipeline.first);
    binding_ = std::move(pipeline.second);
    BufferDesc desc;
    desc.Name = "FORGE scene display settings";
    desc.Size = 32;
    desc.Usage = USAGE_DYNAMIC;
    desc.BindFlags = BIND_UNIFORM_BUFFER;
    desc.CPUAccessFlags = CPU_ACCESS_WRITE;
    device_->CreateBuffer(desc, nullptr, &values_);
    auto* values = binding_->GetVariableByName(SHADER_TYPE_PIXEL, "ForgeDisplay");
    if (!values_ || !values || !binding_->GetVariableByName(SHADER_TYPE_PIXEL, "g_HDR"))
        throw std::runtime_error("Display resolve resource creation failed");
    values->Set(values_);
}
DisplayResolve::~DisplayResolve() = default;
void DisplayResolve::prepare_automatic() {
    if (automatic_)
        return;
    auto automatic = std::make_unique<AutoExposure>(presentation_);
    automatic->prepare();
    auto pipeline = display_pipeline(presentation_, true);
    pipeline.second->GetVariableByName(SHADER_TYPE_PIXEL, "ForgeDisplay")->Set(values_);
    automatic_pipeline_ = std::move(pipeline.first);
    automatic_binding_ = std::move(pipeline.second);
    automatic_ = std::move(automatic);
}
void DisplayResolve::prepare_fast_aa() {
    if (fast_pipeline_)
        return;
    ShaderCreateInfo shader;
    shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    shader.EntryPoint = "main";
    shader.Desc.Name = "FORGE spatial edge filter triangle";
    shader.Desc.ShaderType = SHADER_TYPE_VERTEX;
    shader.Source = fullscreen_triangle_vertex;
    RefCntAutoPtr<IShader> vertex, pixel;
    presentation_.shader(shader, &vertex);
    shader.Desc.Name = "FORGE fast approximate anti-aliasing";
    shader.Desc.ShaderType = SHADER_TYPE_PIXEL;
    shader.Source = fast_approximate_aa_pixel;
    presentation_.shader(shader, &pixel);
    GraphicsPipelineStateCreateInfo info;
    info.PSODesc.Name = "FORGE display spatial edge filter";
    info.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    const ShaderResourceVariableDesc source{SHADER_TYPE_PIXEL, "AASource",
                                            SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC};
    info.PSODesc.ResourceLayout.Variables = &source;
    info.PSODesc.ResourceLayout.NumVariables = 1;
    SamplerDesc sampler;
    sampler.MinFilter = sampler.MagFilter = FILTER_TYPE_LINEAR;
    sampler.MipFilter = FILTER_TYPE_POINT;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = TEXTURE_ADDRESS_CLAMP;
    const ImmutableSamplerDesc immutable{SHADER_TYPE_PIXEL, "AALinear", sampler};
    info.PSODesc.ResourceLayout.ImmutableSamplers = &immutable;
    info.PSODesc.ResourceLayout.NumImmutableSamplers = 1;
    auto& graphics = info.GraphicsPipeline;
    graphics.NumRenderTargets = 1;
    graphics.RTVFormats[0] = TEX_FORMAT_RGBA8_UNORM;
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
        throw std::runtime_error("Fast approximate anti-aliasing pipeline is unavailable");
    pipeline->CreateShaderResourceBinding(&binding, true);
    if (!binding || !binding->GetVariableByName(SHADER_TYPE_PIXEL, "AASource") ||
        !binding->GetVariableByName(SHADER_TYPE_PIXEL, "AAValues"))
        throw std::runtime_error("Fast approximate anti-aliasing shader interface is incomplete");
    BufferDesc buffer;
    buffer.Name = "FORGE spatial edge filter parameters";
    buffer.Size = sizeof(float) * 4;
    buffer.Usage = USAGE_DYNAMIC;
    buffer.BindFlags = BIND_UNIFORM_BUFFER;
    buffer.CPUAccessFlags = CPU_ACCESS_WRITE;
    RefCntAutoPtr<IBuffer> values;
    device_->CreateBuffer(buffer, nullptr, &values);
    if (!values)
        throw std::runtime_error("Fast approximate anti-aliasing parameter allocation failed");
    binding->GetVariableByName(SHADER_TYPE_PIXEL, "AAValues")->Set(values);
    fast_pipeline_ = std::move(pipeline);
    fast_binding_ = std::move(binding);
    fast_values_ = std::move(values);
}
void DisplayResolve::filter_fast_aa(IDeviceContext* context,
                                    const SceneFastApproximateAntiAliasing& settings) {
    const auto& input = output_->GetDesc();
    {
        MapHelper<float> values(context, fast_values_, MAP_WRITE, MAP_FLAG_DISCARD);
        values[0] = float(input.Width);
        values[1] = float(input.Height);
        values[2] = settings.subpixel_strength;
        values[3] = float(settings.search_steps);
    }
    auto* target = spatial_output_->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
    context->SetRenderTargets(1, &target, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    fast_binding_->GetVariableByName(SHADER_TYPE_PIXEL, "AASource")
        ->Set(output_->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
    context->SetPipelineState(fast_pipeline_);
    context->CommitShaderResources(fast_binding_, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    Diligent::Viewport area{0, 0, float(input.Width), float(input.Height), 0, 1};
    context->SetViewports(1, &area, input.Width, input.Height);
    context->Draw(DrawAttribs{3, DRAW_FLAG_VERIFY_ALL});
    fast_binding_->GetVariableByName(SHADER_TYPE_PIXEL, "AASource")->Set(nullptr);
}
void DisplayResolve::prepare_morphological_aa() {
    if (morphological_pipelines_[2])
        return;
    ShaderCreateInfo shader;
    shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    shader.EntryPoint = "main";
    shader.Desc.Name = "FORGE morphological fullscreen triangle";
    shader.Desc.ShaderType = SHADER_TYPE_VERTEX;
    shader.Source = fullscreen_triangle_vertex;
    RefCntAutoPtr<IShader> vertex;
    presentation_.shader(shader, &vertex);
    std::array<RefCntAutoPtr<IPipelineState>, 3> pipelines;
    std::array<RefCntAutoPtr<IShaderResourceBinding>, 3> bindings;
    const char* names[] = {"edges", "weights", "blend"};
    for (unsigned i = 0; i < 3; ++i) {
        shader.EntryPoint = names[i];
        shader.Desc.Name = names[i];
        shader.Desc.ShaderType = SHADER_TYPE_PIXEL;
        shader.Source = morphological_aa_pixel;
        RefCntAutoPtr<IShader> pixel;
        presentation_.shader(shader, &pixel);
        GraphicsPipelineStateCreateInfo info;
        info.PSODesc.Name = names[i];
        info.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
        const ShaderResourceVariableDesc sources[] = {
            {SHADER_TYPE_PIXEL, i == 1 ? "MorphEdges" : "AASource",
             SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
            {SHADER_TYPE_PIXEL, "MorphWeights", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC}};
        info.PSODesc.ResourceLayout.Variables = sources;
        info.PSODesc.ResourceLayout.NumVariables = i == 2 ? 2 : 1;
        auto& graphics = info.GraphicsPipeline;
        graphics.NumRenderTargets = 1;
        graphics.RTVFormats[0] = i == 1 ? TEX_FORMAT_RGBA16_FLOAT : TEX_FORMAT_RGBA8_UNORM;
        graphics.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        graphics.RasterizerDesc.CullMode = CULL_MODE_NONE;
        graphics.DepthStencilDesc.DepthEnable = graphics.DepthStencilDesc.DepthWriteEnable = false;
        info.pVS = vertex;
        info.pPS = pixel;
        presentation_.graphics(info, &pipelines[i]);
        if (!pipelines[i])
            throw std::runtime_error("Morphological anti-aliasing pipeline unavailable");
        pipelines[i]->CreateShaderResourceBinding(&bindings[i], true);
        if (!bindings[i] || !bindings[i]->GetVariableByName(SHADER_TYPE_PIXEL, sources[0].Name) ||
            !bindings[i]->GetVariableByName(SHADER_TYPE_PIXEL, "AAValues") ||
            (i == 2 && !bindings[i]->GetVariableByName(SHADER_TYPE_PIXEL, "MorphWeights")))
            throw std::runtime_error("Morphological anti-aliasing shader interface incomplete");
    }
    BufferDesc desc;
    desc.Name = "FORGE morphological filter parameters";
    desc.Size = sizeof(float) * 8;
    desc.Usage = USAGE_DYNAMIC;
    desc.BindFlags = BIND_UNIFORM_BUFFER;
    desc.CPUAccessFlags = CPU_ACCESS_WRITE;
    RefCntAutoPtr<IBuffer> values;
    device_->CreateBuffer(desc, nullptr, &values);
    if (!values)
        throw std::runtime_error("Morphological anti-aliasing parameter allocation failed");
    for (auto& binding : bindings)
        binding->GetVariableByName(SHADER_TYPE_PIXEL, "AAValues")->Set(values);
    morphological_pipelines_ = std::move(pipelines);
    morphological_bindings_ = std::move(bindings);
    morphological_values_ = std::move(values);
}
void DisplayResolve::filter_morphological_aa(IDeviceContext* context,
                                             const SceneMorphologicalAntiAliasing& settings) {
    const auto& desc = output_->GetDesc();
    {
        MapHelper<float> values(context, morphological_values_, MAP_WRITE, MAP_FLAG_DISCARD);
        values[0] = float(desc.Width);
        values[1] = float(desc.Height);
        values[2] = settings.edge_threshold;
        values[3] = float(settings.search_steps);
        values[4] = settings.corner_rounding;
        values[5] = values[6] = values[7] = 0;
    }
    for (unsigned i = 0; i < 3; ++i) {
        auto* target = (i < 2 ? morphological_images_[i] : spatial_output_)
                           ->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        auto* binding = morphological_bindings_[i].RawPtr();
        const char* input = i == 1 ? "MorphEdges" : "AASource";
        auto* source = (i == 1 ? morphological_images_[0] : output_)
                           ->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        context->SetRenderTargets(1, &target, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        binding->GetVariableByName(SHADER_TYPE_PIXEL, input)->Set(source);
        if (i == 2)
            binding->GetVariableByName(SHADER_TYPE_PIXEL, "MorphWeights")
                ->Set(morphological_images_[1]->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
        context->SetPipelineState(morphological_pipelines_[i]);
        context->CommitShaderResources(binding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Diligent::Viewport area{0, 0, float(desc.Width), float(desc.Height), 0, 1};
        context->SetViewports(1, &area, desc.Width, desc.Height);
        context->Draw(DrawAttribs{3, DRAW_FLAG_VERIFY_ALL});
        binding->GetVariableByName(SHADER_TYPE_PIXEL, input)->Set(nullptr);
        if (i == 2)
            binding->GetVariableByName(SHADER_TYPE_PIXEL, "MorphWeights")->Set(nullptr);
    }
}
ITextureView* DisplayResolve::resolve(IDeviceContext* context, ITextureView* source,
                                      float exposure_ev, const SceneColorAdjustments& color,
                                      const SceneAutoExposure& automatic, float delta, bool reset,
                                      const SceneFastApproximateAntiAliasing& fast,
                                      const SceneMorphologicalAntiAliasing& morphological) {
    validate_scene_color_adjustments(color);
    validate_scene_auto_exposure(automatic);
    validate_scene_fast_approximate_anti_aliasing(fast);
    validate_scene_morphological_anti_aliasing(morphological);
    if (fast.enabled && morphological.enabled)
        throw std::runtime_error("Choose one spatial anti-aliasing method");
    if (!context || !source || !std::isfinite(exposure_ev))
        throw std::runtime_error(
            "Display resolve requires a context, HDR source and finite exposure");
    const double exposure = std::exp2(double(exposure_ev));
    if (!std::isfinite(exposure) || exposure > std::numeric_limits<float>::max())
        throw std::runtime_error("Display exposure exceeds finite GPU representation");
    const auto extent = display_source_extent(source);
    const auto width = extent[0], height = extent[1];
    if ((fast.enabled || morphological.enabled) && std::uint64_t(width) * height > 8388608)
        throw std::runtime_error("Spatial anti-aliasing exceeds 8,388,608 output pixels; "
                                 "reduce the view size or disable the effect");
    if (fast.enabled)
        prepare_fast_aa();
    if (morphological.enabled)
        prepare_morphological_aa();
    if (automatic.enabled) {
        if (!std::isfinite(delta) || delta < 0 || delta > .25f || context->GetDesc().IsDeferred)
            throw std::runtime_error(
                "Automatic exposure requires its immediate context and bounded elapsed time");
        (void)auto_exposure_plan(width, height,
                                 device_->GetAdapterInfo().Texture.MaxTexture2DDimension);
        prepare_automatic();
        binding_->GetVariableByName(SHADER_TYPE_PIXEL, "g_HDR")->Set(nullptr);
    } else if (automatic_) {
        automatic_->clear_source();
        automatic_binding_->GetVariableByName(SHADER_TYPE_PIXEL, "g_HDR")->Set(nullptr);
    }
    auto next_output = output_;
    auto next_spatial = spatial_output_;
    auto next_morphological = morphological_images_;
    if (!next_output || next_output->GetDesc().Width != width ||
        next_output->GetDesc().Height != height) {
        TextureDesc desc;
        desc.Name = "FORGE sRGB-encoded display output";
        desc.Type = RESOURCE_DIM_TEX_2D;
        desc.Width = width;
        desc.Height = height;
        desc.Format = TEX_FORMAT_RGBA8_UNORM;
        desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
        RefCntAutoPtr<ITexture> candidate;
        device_->CreateTexture(desc, nullptr, &candidate);
        if (!candidate)
            throw std::runtime_error("Display target allocation failed; previous target retained");
        next_output = std::move(candidate);
    }
    // Prepare every requested target before drawing or publishing the new set.
    // Failed optional allocations leave the usable display and scratch images intact.
    if ((fast.enabled || morphological.enabled) &&
        (!next_spatial || next_spatial->GetDesc().Width != width ||
         next_spatial->GetDesc().Height != height)) {
        auto desc = next_output->GetDesc();
        desc.Name = "FORGE anti-aliased sRGB-encoded display output";
        RefCntAutoPtr<ITexture> candidate;
        device_->CreateTexture(desc, nullptr, &candidate);
        if (!candidate)
            throw std::runtime_error("Spatial anti-aliasing target allocation failed; "
                                     "previous targets retained");
        next_spatial = std::move(candidate);
    }
    if (morphological.enabled)
        for (unsigned i = 0; i < 2; ++i) {
            if (next_morphological[i] && next_morphological[i]->GetDesc().Width == width &&
                next_morphological[i]->GetDesc().Height == height)
                continue;
            auto desc = next_output->GetDesc();
            desc.Name = i == 0 ? "FORGE morphological edges" : "FORGE morphological coverage";
            desc.Format = i == 0 ? TEX_FORMAT_RGBA8_UNORM : TEX_FORMAT_RGBA16_FLOAT;
            RefCntAutoPtr<ITexture> candidate;
            device_->CreateTexture(desc, nullptr, &candidate);
            if (!candidate)
                throw std::runtime_error("Morphological anti-aliasing target allocation failed; "
                                         "previous targets retained");
            next_morphological[i] = std::move(candidate);
        }
    morphological_images_ = std::move(next_morphological);
    output_ = std::move(next_output);
    spatial_output_ = std::move(next_spatial);
    {
        MapHelper<float> values(context, values_, MAP_WRITE, MAP_FLAG_DISCARD);
        values[0] = float(exposure);
        // Skip the identity as well as disabled adjustments. Retain the existing
        // arithmetic path/pixels without additional targets, passes or variants.
        values[1] = color.enabled && (color.filter != std::array<float, 3>{1, 1, 1} ||
                                      color.saturation != 1 || color.contrast != 1);
        values[2] = color.saturation;
        values[3] = color.contrast;
        std::copy(color.filter.begin(), color.filter.end(), &values[4]);
        values[7] = 0;
    }
    auto* binding = automatic.enabled ? automatic_binding_.RawPtr() : binding_.RawPtr();
    auto* pipeline = automatic.enabled ? automatic_pipeline_.RawPtr() : pipeline_.RawPtr();
    if (automatic.enabled)
        binding->GetVariableByName(SHADER_TYPE_PIXEL, "g_AutoExposure")
            ->Set(automatic_->apply(context, source, automatic, delta, reset));
    auto* target = output_->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
    context->SetRenderTargets(1, &target, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    binding->GetVariableByName(SHADER_TYPE_PIXEL, "g_HDR")->Set(source);
    context->SetPipelineState(pipeline);
    context->CommitShaderResources(binding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    Diligent::Viewport area{0, 0, float(width), float(height), 0, 1};
    context->SetViewports(1, &area, width, height);
    DrawAttribs draw;
    draw.NumVertices = 3;
    draw.Flags = DRAW_FLAG_VERIFY_ALL;
    context->Draw(draw);
    if (fast.enabled)
        filter_fast_aa(context, fast);
    if (morphological.enabled)
        filter_morphological_aa(context, morphological);
    spatial_active_ = fast.enabled || morphological.enabled;
    return output();
}
ITextureView* DisplayResolve::output() const {
    auto* image = spatial_active_ ? spatial_output_.RawPtr() : output_.RawPtr();
    return image ? image->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE) : nullptr;
}
ITextureView* DisplayResolve::target() const {
    auto* image = spatial_active_ ? spatial_output_.RawPtr() : output_.RawPtr();
    return image ? image->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET) : nullptr;
}
} // namespace forge
