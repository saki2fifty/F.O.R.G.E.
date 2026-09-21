#include "texture_preview.hpp"
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include "Utilities/interface/DiligentFXShaderSourceStreamFactory.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace forge {
using namespace Diligent;
namespace {
struct Values {
    std::array<float, 4> size_exposure_depth;
    std::array<unsigned, 4> channel_display_checker_signed;
    std::array<unsigned, 4> alpha_filter;
    std::array<float, 4> region;
};
static_assert(sizeof(Values) == 64);
} // namespace
TexturePreviewRenderer::TexturePreviewRenderer(DiligentPresentation& presentation)
    : presentation_(presentation) {
    BufferDesc desc;
    desc.Name = "FORGE texture preview controls";
    desc.Size = sizeof(Values);
    desc.Usage = USAGE_DYNAMIC;
    desc.BindFlags = BIND_UNIFORM_BUFFER;
    desc.CPUAccessFlags = CPU_ACCESS_WRITE;
    presentation.device()->CreateBuffer(desc, nullptr, &values_);
    if (!values_)
        throw std::runtime_error("Texture preview control allocation failed");
}
TexturePreviewRenderer::Pipeline& TexturePreviewRenderer::pipeline(unsigned dimension) {
    auto& selected = pipelines_.at(dimension);
    if (selected.state)
        return selected;
    Pipeline candidate;
    ShaderCreateInfo shader;
    shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    shader.ShaderCompiler = SHADER_COMPILER_FXC;
    shader.HLSLVersion = {5, 1};
    shader.EntryPoint = "main";
    shader.pShaderSourceStreamFactory = &DiligentFXShaderSourceStreamFactory::GetInstance();
    shader.Desc.Name = "FORGE texture preview fullscreen triangle";
    shader.Desc.ShaderType = SHADER_TYPE_VERTEX;
    shader.Source = R"(
float4 main(uint id:SV_VertexID):SV_Position {
    return float4(id==2?3:-1,id==1?3:-1,0,1);
})";
    RefCntAutoPtr<IShader> vertex, pixel;
    presentation_.shader(shader, &vertex);
    const std::string source = std::string(dimension == 2   ? "#define VOLUME 1\n"
                                           : dimension == 1 ? "#define ARRAY 1\n"
                                                            : "") +
                               R"(
#define TONE_MAPPING_MODE TONE_MAPPING_MODE_PBR_NEUTRAL
#include "ToneMapping.fxh"
#ifdef VOLUME
Texture3D<float4> g_Source;
#elif defined(ARRAY)
Texture2DArray<float4> g_Source;
#else
Texture2D<float4> g_Source;
#endif
SamplerState g_Linear;
SamplerState g_Nearest;
cbuffer ForgeTexturePreview {float4 g_SizeExposureDepth; uint4 g_Options; uint4 g_AlphaFilter; float4 g_Region;};
float4 main(float4 position:SV_Position):SV_Target0 {
    float2 uv=lerp(g_Region.xy,g_Region.zw,position.xy/g_SizeExposureDepth.xy);
#if defined(VOLUME) || defined(ARRAY)
    float3 coordinate=float3(uv,g_SizeExposureDepth.w);
#else
    float2 coordinate=uv;
#endif
    float4 value=g_AlphaFilter.y!=0 ? g_Source.SampleLevel(g_Nearest,coordinate,0) :
                                    g_Source.SampleLevel(g_Linear,coordinate,0);
    if(!all(isfinite(value)))return float4(1,0,1,1);
    if(g_Options.x>=2) {
        float channel=value[g_Options.x-2];
        if(g_Options.w!=0 && g_Options.x!=5)channel=channel*.5+.5;
        return float4(saturate(channel*g_SizeExposureDepth.z).xxx,1);
    }
    float alpha=g_AlphaFilter.x==0?1:saturate(value.a);
    if(g_AlphaFilter.x==2)value.rgb=alpha>0?value.rgb/alpha:0;
    if(g_Options.w!=0)value.rgb=value.rgb*.5+.5;
    float3 color=value.rgb*g_SizeExposureDepth.z;
    if(!all(isfinite(color)))return float4(1,0,1,1);
    if(g_Options.y==2) {
        ToneMappingAttribs settings=(ToneMappingAttribs)0;
        settings.fMiddleGray=.18;settings.fWhitePoint=3;settings.fLuminanceSaturation=1;
        color=ToneMap(color,settings,.3);
    }
    if(g_Options.x==0 && g_Options.z!=0) {
        float checker=((uint(position.x)/12+uint(position.y)/12)&1)!=0?.3:.2;
        float3 background=checker.xxx;
        if(g_Options.y!=0)background=SRGBToLinear(background);
        color=lerp(background,color,alpha);
    }
    if(g_Options.y!=0)color=LinearToSRGB(max(color,0));
    return float4(saturate(color),1);
})";
    shader.Desc.Name = dimension == 2 ? "FORGE volume slice preview" : "FORGE texture face preview";
    shader.Desc.ShaderType = SHADER_TYPE_PIXEL;
    shader.Source = source.c_str();
    presentation_.shader(shader, &pixel);
    GraphicsPipelineStateCreateInfo ci;
    ci.PSODesc.Name = dimension == 2 ? "FORGE volume preview" : "FORGE texture preview";
    ci.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    const ShaderResourceVariableDesc input{SHADER_TYPE_PIXEL, "g_Source",
                                           SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC};
    ci.PSODesc.ResourceLayout.Variables = &input;
    ci.PSODesc.ResourceLayout.NumVariables = 1;
    SamplerDesc linear;
    linear.AddressU = linear.AddressV = linear.AddressW = TEXTURE_ADDRESS_CLAMP;
    SamplerDesc nearest = linear;
    nearest.MinFilter = nearest.MagFilter = nearest.MipFilter = FILTER_TYPE_POINT;
    const ImmutableSamplerDesc samplers[] = {{SHADER_TYPE_PIXEL, "g_Linear", linear},
                                             {SHADER_TYPE_PIXEL, "g_Nearest", nearest}};
    ci.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
    ci.PSODesc.ResourceLayout.NumImmutableSamplers = 2;
    ci.GraphicsPipeline.NumRenderTargets = 1;
    ci.GraphicsPipeline.RTVFormats[0] = TEX_FORMAT_RGBA8_UNORM;
    ci.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    ci.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_NONE;
    ci.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;
    ci.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = false;
    ci.pVS = vertex;
    ci.pPS = pixel;
    presentation_.graphics(ci, &candidate.state);
    candidate.state->CreateShaderResourceBinding(&candidate.binding, true);
    auto* controls = candidate.binding->GetVariableByName(SHADER_TYPE_PIXEL, "ForgeTexturePreview");
    if (!controls || !candidate.binding->GetVariableByName(SHADER_TYPE_PIXEL, "g_Source"))
        throw std::runtime_error("Texture preview bindings are unavailable");
    controls->Set(values_);
    selected = std::move(candidate);
    return selected;
}
ITextureView* TexturePreviewRenderer::render(IDeviceContext* context, ITexture* texture,
                                             const TexturePreviewSettings& settings, unsigned width,
                                             unsigned height) {
    if (!context || !texture || !width || !height || width > 2048 || height > 2048 ||
        !std::isfinite(settings.exposure) || std::abs(settings.exposure) > 20 ||
        unsigned(settings.channel) > unsigned(TexturePreviewChannel::Alpha) ||
        unsigned(settings.display) > unsigned(TexturePreviewDisplay::Hdr) ||
        unsigned(settings.alpha) > unsigned(TextureAlpha::Custom))
        throw std::runtime_error("Texture preview settings/dimensions exceed supported bounds");
    const auto& input = texture->GetDesc();
    if (!std::all_of(
            settings.region.begin(), settings.region.end(),
            [](float value) { return std::isfinite(value) && value >= 0 && value <= 1; }) ||
        settings.region[0] >= settings.region[2] || settings.region[1] >= settings.region[3])
        throw std::runtime_error(
            "Texture preview crop must be a finite nonempty normalized rectangle");
    const bool volume = input.Type == RESOURCE_DIM_TEX_3D;
    const bool cube =
        input.Type == RESOURCE_DIM_TEX_CUBE || input.Type == RESOURCE_DIM_TEX_CUBE_ARRAY;
    const bool array = input.Type == RESOURCE_DIM_TEX_2D_ARRAY || cube;
    if ((!volume && !array && input.Type != RESOURCE_DIM_TEX_2D) || input.SampleCount != 1 ||
        !(input.BindFlags & BIND_SHADER_RESOURCE) || settings.mip >= input.MipLevels ||
        settings.face >= (cube ? 6u : 1u) ||
        settings.layer >= (array ? input.ArraySize / (cube ? 6u : 1u) : 1u) ||
        settings.depth >= (volume ? std::max(1u, input.Depth >> settings.mip) : 1u))
        throw std::runtime_error("Texture preview subresource is unavailable");
    if (texture == output_)
        throw std::runtime_error("Texture preview cannot read its own output target");
    auto& selected = pipeline(volume ? 2u : array ? 1u : 0u);
    TextureViewDesc view;
    view.Name = "FORGE selected texture preview subresource";
    view.ViewType = TEXTURE_VIEW_SHADER_RESOURCE;
    // D3D12's exact pinned backend forbids nonzero FirstArraySlice on a
    // non-array SRV, even though generic validation permits that view kind.
    view.TextureDim = volume  ? RESOURCE_DIM_TEX_3D
                      : array ? RESOURCE_DIM_TEX_2D_ARRAY
                              : RESOURCE_DIM_TEX_2D;
    view.MostDetailedMip = settings.mip;
    view.NumMipLevels = 1;
    if (!volume) {
        view.FirstArraySlice = settings.layer * (cube ? 6u : 1u) + settings.face;
        view.NumArraySlices = 1;
    }
    RefCntAutoPtr<ITextureView> source;
    texture->CreateView(view, &source);
    if (!source)
        throw std::runtime_error("Texture preview subresource view creation failed");
    RefCntAutoPtr<ITexture> next = output_;
    if (!next || next->GetDesc().Width != width || next->GetDesc().Height != height) {
        TextureDesc desc;
        desc.Name = "FORGE texture preview display";
        desc.Type = RESOURCE_DIM_TEX_2D;
        desc.Width = width;
        desc.Height = height;
        desc.Format = TEX_FORMAT_RGBA8_UNORM;
        desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
        next.Release();
        presentation_.device()->CreateTexture(desc, nullptr, &next);
        if (!next)
            throw std::runtime_error("Texture preview allocation failed; previous image retained");
    }
    {
        MapHelper<Values> values(context, values_, MAP_WRITE, MAP_FLAG_DISCARD);
        values->size_exposure_depth = {
            float(width), float(height), std::exp2(settings.exposure),
            volume ? (settings.depth + .5f) / std::max(1u, input.Depth >> settings.mip) : 0.f};
        values->channel_display_checker_signed = {
            unsigned(settings.channel), unsigned(settings.display), unsigned(settings.checker),
            unsigned(settings.signed_values)};
        values->alpha_filter = {settings.alpha == TextureAlpha::Opaque          ? 0u
                                : settings.alpha == TextureAlpha::Premultiplied ? 2u
                                                                                : 1u,
                                unsigned(settings.nearest), 0, 0};
        values->region = settings.region;
    }
    auto* target = next->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
    context->SetRenderTargets(1, &target, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    selected.binding->GetVariableByName(SHADER_TYPE_PIXEL, "g_Source")->Set(source);
    context->SetPipelineState(selected.state);
    context->CommitShaderResources(selected.binding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    Diligent::Viewport area{0, 0, float(width), float(height), 0, 1};
    context->SetViewports(1, &area, width, height);
    DrawAttribs draw;
    draw.NumVertices = 3;
    draw.Flags = DRAW_FLAG_VERIFY_ALL;
    context->Draw(draw);
    ++renders_;
    // The command submission owns its native references. Do not let a dormant
    // SRB retain an input after the caller releases its accounted GPU lease.
    selected.binding->GetVariableByName(SHADER_TYPE_PIXEL, "g_Source")->Set(nullptr);
    output_ = std::move(next);
    return output();
}
ITextureView* TexturePreviewRenderer::output() const {
    return output_ ? output_->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE) : nullptr;
}
} // namespace forge
