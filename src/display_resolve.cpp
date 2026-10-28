#include "display_resolve.hpp"
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include "Utilities/interface/DiligentFXShaderSourceStreamFactory.hpp"
#include "auto_exposure.hpp"
#include "display_source.hpp"
#include <algorithm>
#include <cmath>
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
    shader.Source = R"(
float4 main(uint id:SV_VertexID):SV_Position {
    return float4(id==2?3:-1,id==1?3:-1,0,1);
})";
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
ITextureView* DisplayResolve::resolve(IDeviceContext* context, ITextureView* source,
                                      float exposure_ev, const SceneColorAdjustments& color,
                                      const SceneAutoExposure& automatic, float delta, bool reset) {
    validate_scene_color_adjustments(color);
    validate_scene_auto_exposure(automatic);
    if (!context || !source || !std::isfinite(exposure_ev))
        throw std::runtime_error(
            "Display resolve requires a context, HDR source and finite exposure");
    const double exposure = std::exp2(double(exposure_ev));
    if (!std::isfinite(exposure) || exposure > std::numeric_limits<float>::max())
        throw std::runtime_error("Display exposure exceeds finite GPU representation");
    const auto extent = display_source_extent(source);
    const auto width = extent[0], height = extent[1];
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
    if (!output_ || output_->GetDesc().Width != width || output_->GetDesc().Height != height) {
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
        output_ = std::move(candidate);
    }
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
    return output();
}
ITextureView* DisplayResolve::output() const {
    return output_ ? output_->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE) : nullptr;
}
ITextureView* DisplayResolve::target() const {
    return output_ ? output_->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET) : nullptr;
}
} // namespace forge
