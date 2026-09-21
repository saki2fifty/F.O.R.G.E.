#include "presentation_diligent.hpp"
#include "Graphics/Archiver/interface/ArchiverFactoryLoader.h"
#include <stdexcept>
namespace forge {
using namespace Diligent;
DiligentPresentation::DiligentPresentation(IRenderDevice* device) : device_(device) {
    if (!device)
        throw std::runtime_error("Presentation requires a render device");
    auto* factory = LoadAndGetArchiverFactory();
    if (!factory)
        throw std::runtime_error("Diligent render-state archiver is unavailable");
    RenderStateCacheCreateInfo info{device, factory};
    info.FileHashMode = RENDER_STATE_CACHE_FILE_HASH_MODE_BY_CONTENT;
    info.EnableHotReload = false;
    info.LogLevel = RENDER_STATE_CACHE_LOG_LEVEL_DISABLED;
    CreateRenderStateCache(info, &cache_);
    if (!cache_)
        throw std::runtime_error("Diligent render-state cache creation failed");
}
void DiligentPresentation::clear_cache() {
    cache_->Reset();
    epoch_creations_ = 0;
}
void DiligentPresentation::trim() {
    // Bound retained serialization entries across repeated authoring changes.
    // This is an entry budget, not a claim about a fixed byte/VRAM ceiling.
    if (epoch_creations_ >= 256)
        clear_cache();
}
void DiligentPresentation::shader(const ShaderCreateInfo& info, IShader** result) {
    if (!result)
        throw std::runtime_error("Presentation output pointer is null");
    trim();
    const bool hit = cache_->CreateShader(info, result);
    if (!result || !*result)
        throw std::runtime_error("Diligent presentation shader creation failed");
    hits_ += hit;
    misses_ += !hit;
    epoch_creations_ += !hit;
}
void DiligentPresentation::graphics(const GraphicsPipelineStateCreateInfo& info,
                                    IPipelineState** result) {
    if (!result)
        throw std::runtime_error("Presentation output pointer is null");
    trim();
    const bool hit = cache_->CreateGraphicsPipelineState(info, result);
    if (!result || !*result)
        throw std::runtime_error("Diligent presentation pipeline creation failed");
    hits_ += hit;
    misses_ += !hit;
    epoch_creations_ += !hit;
}
void DiligentPresentation::compute(const ComputePipelineStateCreateInfo& info,
                                   IPipelineState** result) {
    if (!result)
        throw std::runtime_error("Presentation output pointer is null");
    trim();
    const bool hit = cache_->CreateComputePipelineState(info, result);
    if (!*result)
        throw std::runtime_error("Diligent presentation compute pipeline creation failed");
    hits_ += hit;
    misses_ += !hit;
    epoch_creations_ += !hit;
}
PBR_Renderer& DiligentPresentation::pbr(IDeviceContext* context) {
    if (!context)
        throw std::runtime_error("PBR initialization requires a device context");
    if (!pbr_) {
        PBR_Renderer::CreateInfo info;
        info.EnableSheen = true;
        info.MaxJointCount = 0;
        info.MaxActiveMorphTargetCount = 0;
        info.CreateDefaultJointsBuffer = false;
        info.CreateDefaultMorphTargetBuffer = false;
        info.AllowHotShaderReload = false;
        // Native lighting/LUT/convolution utilities only. FORGE supplies validated
        // signed/zero-safe vertex transforms and per-material resource bindings.
        auto candidate = std::make_unique<PBR_Renderer>(device_, cache_, context, info, false);
        if (!candidate->GetPreintegratedGGX_SRV() || !candidate->GetPreintegratedSheen_SRV() ||
            !candidate->GetWhiteTexSRV() || !candidate->GetDefaultNormalMapSRV())
            throw std::runtime_error("Diligent PBR resource initialization failed");
        pbr_ = std::move(candidate);
    }
    return *pbr_;
}
ITextureView* DiligentPresentation::black_environment(IDeviceContext* context) {
    if (!black_environment_) {
        auto candidate = pbr(context).CreateIrradianceCube(context, "FORGE disabled environment",
                                                           TEX_FORMAT_RGBA16_FLOAT, 1);
        if (!candidate)
            throw std::runtime_error("Default environment allocation failed");
        black_environment_ = std::move(candidate);
    }
    return black_environment_->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
}
ITextureView* DiligentPresentation::empty_shadow(IDeviceContext* context) {
    if (!empty_shadow_) {
        TextureDesc desc;
        desc.Name = "FORGE disabled shadow";
        desc.Type = RESOURCE_DIM_TEX_2D_ARRAY;
        desc.Width = desc.Height = desc.ArraySize = 1;
        desc.Format = TEX_FORMAT_D32_FLOAT;
        desc.BindFlags = BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE;
        RefCntAutoPtr<ITexture> candidate;
        device_->CreateTexture(desc, nullptr, &candidate);
        if (!candidate)
            throw std::runtime_error("Disabled shadow allocation failed");
        context->ClearDepthStencil(candidate->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL),
                                   CLEAR_DEPTH_FLAG, 1, 0,
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        empty_shadow_ = std::move(candidate);
    }
    return empty_shadow_->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
}
} // namespace forge
