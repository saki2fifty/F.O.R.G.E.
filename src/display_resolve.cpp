#include "display_resolve.hpp"
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include "Utilities/interface/DiligentFXShaderSourceStreamFactory.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>
namespace forge {
using namespace Diligent;
DisplayResolve::DisplayResolve(DiligentPresentation& presentation)
    : device_(presentation.device()) {
    ShaderCreateInfo shader;
    shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    shader.ShaderCompiler = SHADER_COMPILER_FXC;
    shader.HLSLVersion = {5, 1};
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
    shader.Desc.Name = "FORGE PBR Neutral display resolve";
    shader.Desc.ShaderType = SHADER_TYPE_PIXEL;
    shader.Source = R"(
#define TONE_MAPPING_MODE TONE_MAPPING_MODE_PBR_NEUTRAL
#include "ToneMapping.fxh"
Texture2D<float4> g_HDR;
cbuffer ForgeDisplay {float4 g_Display;};
float4 main(float4 position:SV_Position):SV_Target0 {
    float4 source=g_HDR.Load(int3(int2(position.xy),0));
    float3 exposed=source.rgb*g_Display.x;
    if(!all(isfinite(source))||!all(isfinite(exposed)))return float4(1,0,1,1);
    ToneMappingAttribs settings=(ToneMappingAttribs)0;
    settings.fMiddleGray=.18;settings.fWhitePoint=3;settings.fLuminanceSaturation=1;
    // PBR Neutral uses .3/average luminance. Manual exposure is applied above;
    // .3 therefore leaves that exposure unchanged. No automatic eye adaptation.
    float3 mapped=ToneMap(exposed,settings,.3);
    return float4(LinearToSRGB(mapped),saturate(source.a));
})";
    presentation.shader(shader, &pixel);
    GraphicsPipelineStateCreateInfo ci;
    ci.PSODesc.Name = "FORGE HDR to display";
    ci.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    ci.GraphicsPipeline.NumRenderTargets = 1;
    ci.GraphicsPipeline.RTVFormats[0] = TEX_FORMAT_RGBA8_UNORM;
    ci.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    ci.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_NONE;
    ci.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;
    ci.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = false;
    ci.pVS = vertex;
    ci.pPS = pixel;
    presentation.graphics(ci, &pipeline_);
    pipeline_->CreateShaderResourceBinding(&binding_, true);
    BufferDesc desc;
    desc.Name = "FORGE manual display exposure";
    desc.Size = 16;
    desc.Usage = USAGE_DYNAMIC;
    desc.BindFlags = BIND_UNIFORM_BUFFER;
    desc.CPUAccessFlags = CPU_ACCESS_WRITE;
    device_->CreateBuffer(desc, nullptr, &values_);
    auto* values = binding_->GetVariableByName(SHADER_TYPE_PIXEL, "ForgeDisplay");
    if (!values_ || !values || !binding_->GetVariableByName(SHADER_TYPE_PIXEL, "g_HDR"))
        throw std::runtime_error("Display resolve resource creation failed");
    values->Set(values_);
}
ITextureView* DisplayResolve::resolve(IDeviceContext* context, ITextureView* source,
                                      float exposure_ev) {
    if (!context || !source || !std::isfinite(exposure_ev))
        throw std::runtime_error(
            "Display resolve requires a context, HDR source and finite exposure");
    const double exposure = std::exp2(double(exposure_ev));
    if (!std::isfinite(exposure) || exposure > std::numeric_limits<float>::max())
        throw std::runtime_error("Display exposure exceeds finite GPU representation");
    const auto& input = source->GetTexture()->GetDesc();
    if (input.Type != RESOURCE_DIM_TEX_2D || input.SampleCount != 1 ||
        (input.Format != TEX_FORMAT_RGBA16_FLOAT && input.Format != TEX_FORMAT_RGBA32_FLOAT))
        throw std::runtime_error(
            "Display resolve requires a single-sample linear floating-point RGBA target");
    if (!output_ || output_->GetDesc().Width != input.Width ||
        output_->GetDesc().Height != input.Height) {
        TextureDesc desc;
        desc.Name = "FORGE sRGB-encoded display output";
        desc.Type = RESOURCE_DIM_TEX_2D;
        desc.Width = input.Width;
        desc.Height = input.Height;
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
        values[1] = values[2] = values[3] = 0;
    }
    auto* target = output_->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
    context->SetRenderTargets(1, &target, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    binding_->GetVariableByName(SHADER_TYPE_PIXEL, "g_HDR")->Set(source);
    context->SetPipelineState(pipeline_);
    context->CommitShaderResources(binding_, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    Diligent::Viewport area{0, 0, float(input.Width), float(input.Height), 0, 1};
    context->SetViewports(1, &area, input.Width, input.Height);
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
