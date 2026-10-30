#include "weighted_transparency.hpp"
#include "Utilities/interface/DiligentFXShaderSourceStreamFactory.hpp"
#include "fullscreen_triangle_shader.hpp"
#include "presentation_views.hpp"
#include <stdexcept>
namespace forge {
using namespace Diligent;
namespace {
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
void targets(ITexture* color, ITextureView* depth, PixelViewport area) {
    require(color && (color->GetDesc().Format == TEX_FORMAT_RGBA16_FLOAT ||
                      color->GetDesc().Format == TEX_FORMAT_RGBA32_FLOAT),
            "Weighted transparency requires a linear floating-point HDR target");
    require(complete_surface_view(color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET),
                                  color->GetDesc().Format, TEXTURE_VIEW_RENDER_TARGET) &&
                complete_surface_view(depth, TEX_FORMAT_D32_FLOAT, TEXTURE_VIEW_DEPTH_STENCIL),
            "Weighted transparency requires complete single-sample color and D32 views");
    const auto& c = color->GetDesc();
    const auto& d = depth->GetTexture()->GetDesc();
    require(c.Width == d.Width && c.Height == d.Height && area.width && area.height &&
                std::uint64_t(area.x) + area.width <= c.Width &&
                std::uint64_t(area.y) + area.height <= c.Height,
            "Weighted transparency target extents or viewport differ");
}
} // namespace
WeightedTransparency::WeightedTransparency(DiligentPresentation& presentation, std::uint64_t budget)
    : presentation_(presentation), budget_(budget) {
    require(budget && budget <= weighted_transparency_payload_limit,
            "Invalid weighted transparency payload budget");
}
void WeightedTransparency::prepare(ITexture* color, ITextureView* depth, PixelViewport area) {
    targets(color, depth, area);
    const auto& size = color->GetDesc();
    require(size.Width <= budget_ / 24 / size.Height,
            "Weighted transparency attachments exceed their 512 MiB payload profile");
    if (radiance_ && radiance_->GetDesc().Width == size.Width &&
        radiance_->GetDesc().Height == size.Height && color_format_ == size.Format) {
        area_ = area;
        return;
    }
    auto* device = presentation_.device();
    const auto texture = [&](TEXTURE_FORMAT format, const char* name) {
        const auto flags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
        require((device->GetTextureFormatInfoExt(format).BindFlags & flags) == flags,
                "Device lacks weighted transparency target format support");
        TextureDesc desc;
        desc.Name = name;
        desc.Type = RESOURCE_DIM_TEX_2D;
        desc.Width = size.Width;
        desc.Height = size.Height;
        desc.MipLevels = 1;
        desc.Format = format;
        desc.BindFlags = flags;
        RefCntAutoPtr<ITexture> result;
        device->CreateTexture(desc, nullptr, &result);
        require(bool(result), "Weighted transparency attachment allocation failed");
        return result;
    };
    auto radiance = texture(TEX_FORMAT_RGBA32_FLOAT, "FORGE weighted transparency radiance");
    auto optical = texture(TEX_FORMAT_RG32_FLOAT, "FORGE weighted transparency optical coverage");
    ShaderCreateInfo shader;
    shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    shader.EntryPoint = "main";
    shader.pShaderSourceStreamFactory = &DiligentFXShaderSourceStreamFactory::GetInstance();
    shader.Desc.Name = "FORGE weighted transparency triangle";
    shader.Desc.ShaderType = SHADER_TYPE_VERTEX;
    shader.Source = fullscreen_triangle_vertex;
    RefCntAutoPtr<IShader> vertex, pixel;
    presentation_.shader(shader, &vertex);
    shader.Desc.Name = "FORGE weighted transparency resolve";
    shader.Desc.ShaderType = SHADER_TYPE_PIXEL;
    shader.Source = R"(
#include "ForgeTransparency.fxh"
Texture2D<float4> g_Accumulation;
Texture2D<float2> g_Optical;
float4 main(float4 position:SV_Position):SV_Target0 {
    int3 at=int3(int2(position.xy),0);
    float4 sum=g_Accumulation.Load(at);float2 optical=g_Optical.Load(at);
    if(!all(isfinite(sum)) || !all(isfinite(optical)) || sum.a<0 || any(optical<0))
        return float4(1,0,1,1);
    if(sum.a==0)return 0;
    float coverage=ForgeTransparencyCoverage(optical);
    return float4((sum.rgb/sum.a)*coverage,coverage);
})";
    presentation_.shader(shader, &pixel);
    require(vertex && pixel, "Weighted transparency resolve shader unavailable");
    GraphicsPipelineStateCreateInfo ci;
    ci.PSODesc.Name = "FORGE weighted transparency composition";
    ci.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    ci.pVS = vertex;
    ci.pPS = pixel;
    auto& p = ci.GraphicsPipeline;
    p.NumRenderTargets = 1;
    p.RTVFormats[0] = size.Format;
    p.DSVFormat = TEX_FORMAT_D32_FLOAT;
    p.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    p.RasterizerDesc.CullMode = CULL_MODE_NONE;
    p.DepthStencilDesc.DepthEnable = p.DepthStencilDesc.DepthWriteEnable = false;
    auto& blend = p.BlendDesc.RenderTargets[0];
    blend.BlendEnable = true;
    blend.SrcBlend = blend.SrcBlendAlpha = BLEND_FACTOR_ONE;
    blend.DestBlend = blend.DestBlendAlpha = BLEND_FACTOR_INV_SRC_ALPHA;
    RefCntAutoPtr<IPipelineState> pipeline;
    presentation_.graphics(ci, &pipeline);
    require(bool(pipeline), "Weighted transparency composition pipeline unavailable");
    RefCntAutoPtr<IShaderResourceBinding> binding;
    pipeline->CreateShaderResourceBinding(&binding, true);
    require(binding && binding->GetVariableByName(SHADER_TYPE_PIXEL, "g_Accumulation") &&
                binding->GetVariableByName(SHADER_TYPE_PIXEL, "g_Optical"),
            "Weighted transparency resolve interface is incomplete");
    binding->GetVariableByName(SHADER_TYPE_PIXEL, "g_Accumulation")
        ->Set(radiance->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
    binding->GetVariableByName(SHADER_TYPE_PIXEL, "g_Optical")
        ->Set(optical->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
    // Publish only the complete attachment/pipeline/binding candidate.
    binding_ = std::move(binding);
    pipeline_ = std::move(pipeline);
    radiance_ = std::move(radiance);
    optical_ = std::move(optical);
    color_format_ = size.Format;
    area_ = area;
}
void WeightedTransparency::viewport(IDeviceContext* context) const {
    Viewport view{float(area_.x), float(area_.y), float(area_.width), float(area_.height), 0, 1};
    context->SetViewports(1, &view, radiance_->GetDesc().Width, radiance_->GetDesc().Height);
}
void WeightedTransparency::begin(IDeviceContext* context, ITextureView* depth) {
    require(context && binding_, "Weighted transparency is not prepared");
    ITextureView* views[]{radiance_->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET),
                          optical_->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET)};
    context->SetRenderTargets(2, views, depth, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    const float zero[4]{};
    for (auto* view : views)
        context->ClearRenderTarget(view, zero, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    viewport(context);
}
void WeightedTransparency::composite(IDeviceContext* context, ITexture* color,
                                     ITextureView* depth) {
    require(context && binding_, "Weighted transparency is not prepared");
    targets(color, depth, area_);
    require(color->GetDesc().Format == color_format_ &&
                color->GetDesc().Width == radiance_->GetDesc().Width &&
                color->GetDesc().Height == radiance_->GetDesc().Height,
            "Weighted transparency composition target changed after preparation");
    auto* view = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
    context->SetRenderTargets(1, &view, depth, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    viewport(context);
    context->SetPipelineState(pipeline_);
    context->CommitShaderResources(binding_, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->Draw(DrawAttribs{3, DRAW_FLAG_VERIFY_ALL});
}
void WeightedTransparency::clear() {
    binding_.Release();
    pipeline_.Release();
    radiance_.Release();
    optical_.Release();
    color_format_ = TEX_FORMAT_UNKNOWN;
    area_ = {};
}
} // namespace forge
