#include "transmission_background.hpp"
#include <algorithm>
#include <bit>
namespace forge {
using namespace Diligent;
ITextureView* TransmissionBackground::output() const {
    return texture_ ? texture_->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE) : nullptr;
}
TransmissionLighting TransmissionBackground::capture(IDeviceContext* context, ITexture* source,
                                                     PixelViewport viewport) {
    if (!context || !source || source == texture_.RawPtr())
        throw std::runtime_error("Transmission snapshot needs a distinct HDR source and context");
    const auto& input = source->GetDesc();
    if (input.Type != RESOURCE_DIM_TEX_2D || input.Format != TEX_FORMAT_RGBA16_FLOAT ||
        input.SampleCount != 1 || !viewport.width || !viewport.height ||
        viewport.x >= input.Width || viewport.y >= input.Height ||
        viewport.width > input.Width - viewport.x || viewport.height > input.Height - viewport.y)
        throw std::runtime_error("Transmission snapshot requires a valid HDR camera rectangle");
    const auto maximum = device_->GetAdapterInfo().Texture.MaxTexture2DDimension;
    if (viewport.width > maximum || viewport.height > maximum)
        throw std::runtime_error("Transmission snapshot exceeds device dimensions");
    const auto& format = device_->GetTextureFormatInfoExt(TEX_FORMAT_RGBA16_FLOAT);
    const auto flags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET | BIND_UNORDERED_ACCESS;
    if ((format.BindFlags & flags) != flags)
        throw std::runtime_error("Device cannot generate the HDR transmission mip chain");
    const unsigned mips = std::bit_width(std::max(viewport.width, viewport.height));
    std::uint64_t bytes = 0;
    for (unsigned level = 0; level < mips; ++level) {
        const auto w = std::max(1u, viewport.width >> level);
        const auto h = std::max(1u, viewport.height >> level);
        if (w > (budget_ - bytes) / 8 / h)
            throw std::runtime_error("Transmission snapshot exceeds the frame payload budget");
        bytes += std::uint64_t(w) * h * 8;
    }
    RefCntAutoPtr<ITexture> candidate = texture_;
    if (!candidate || candidate->GetDesc().Width != viewport.width ||
        candidate->GetDesc().Height != viewport.height) {
        TextureDesc desc;
        desc.Name = "FORGE opaque transmission background";
        desc.Type = RESOURCE_DIM_TEX_2D;
        desc.Width = viewport.width;
        desc.Height = viewport.height;
        desc.MipLevels = mips;
        desc.Format = TEX_FORMAT_RGBA16_FLOAT;
        desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
        desc.MiscFlags = MISC_TEXTURE_FLAG_GENERATE_MIPS;
        candidate.Release(); // Native output pointer is empty; texture_ keeps the previous image.
        device_->CreateTexture(desc, nullptr, &candidate);
        if (!candidate)
            throw std::runtime_error("Transmission snapshot allocation failed");
    }
    Box area{viewport.x, viewport.x + viewport.width, viewport.y, viewport.y + viewport.height};
    CopyTextureAttribs copy{source, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, candidate,
                            RESOURCE_STATE_TRANSITION_MODE_TRANSITION};
    copy.pSrcBox = &area;
    context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
    context->CopyTexture(copy);
    context->GenerateMips(candidate->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
    texture_ = std::move(candidate);
    return {output(), viewport};
}
} // namespace forge
