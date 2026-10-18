#include "presentation_diligent.hpp"
#include "view_image_limits.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
namespace forge {
using namespace Diligent;
void DiligentPresentation::readback(IDeviceContext* context, ITextureView* view) {
    if (thread_ != std::this_thread::get_id() || !context || context->GetDesc().IsDeferred || !view)
        throw std::runtime_error("View image capture requires the presentation context");
    if (readback_pending())
        throw std::runtime_error("A view image capture is already pending");
    if (submitted_frames_ == std::numeric_limits<std::uint64_t>::max())
        throw std::runtime_error("View image capture frame limit reached");
    auto* source = view->GetTexture();
    const auto& input = source->GetDesc();
    const auto& subresource = view->GetDesc();
    if (input.Type != RESOURCE_DIM_TEX_2D || input.SampleCount != 1 || input.MipLevels != 1 ||
        input.ArraySize != 1 || input.Format != TEX_FORMAT_RGBA8_UNORM || !input.Width ||
        !input.Height || input.Width > FORGE_VIEW_IMAGE_MAX_EXTENT ||
        input.Height > FORGE_VIEW_IMAGE_MAX_EXTENT || subresource.MostDetailedMip != 0 ||
        subresource.FirstArraySlice != 0 || subresource.NumMipLevels != 1 ||
        subresource.NumArraySlices != 1)
        throw std::runtime_error("View image capture requires a complete RGBA8 display target");
    auto desc = input;
    desc.Name = "FORGE view image readback";
    desc.Usage = USAGE_STAGING;
    desc.BindFlags = BIND_NONE;
    desc.CPUAccessFlags = CPU_ACCESS_READ;
    desc.MiscFlags = MISC_TEXTURE_FLAG_NONE;
    RefCntAutoPtr<ITexture> staging;
    device_->CreateTexture(desc, nullptr, &staging);
    if (!staging)
        throw std::runtime_error("View image staging allocation failed");
    context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
    CopyTextureAttribs copy{source, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, staging,
                            RESOURCE_STATE_TRANSITION_MODE_TRANSITION};
    context->CopyTexture(copy);
    readback_source_ = source;
    readback_staging_ = std::move(staging);
    readback_context_ = context;
    readback_frame_ = submitted_frames_ + 1;
    readback_discard_ = false;
}
void DiligentPresentation::clear_readback() {
    readback_source_.Release();
    readback_staging_.Release();
    readback_context_.Release();
    readback_frame_ = 0;
    readback_discard_ = false;
}
void DiligentPresentation::cancel_readback() {
    if (thread_ != std::this_thread::get_id())
        throw std::runtime_error("View image cancellation requires the presentation thread");
    readback_discard_ = true;
}
std::optional<DiligentPresentation::DisplayPixels>
DiligentPresentation::take_readback(IDeviceContext* context) {
    if (thread_ != std::this_thread::get_id())
        throw std::runtime_error("View image readback requires the presentation thread");
    if (!readback_pending())
        return {};
    if (!context || context != readback_context_)
        throw std::runtime_error("View image readback context changed");
    if (frame_fence_->GetCompletedValue() < readback_frame_)
        return {};
    if (readback_discard_) {
        clear_readback();
        return {};
    }
    const auto& desc = readback_staging_->GetDesc();
    DisplayPixels result{desc.Width, desc.Height, {}};
    try {
        // Allocate before mapping so allocation failure cannot leave the resource mapped.
        result.rgba.resize(std::size_t(desc.Width) * desc.Height * 4);
        MappedTextureSubresource mapped;
        context->MapTextureSubresource(readback_staging_, 0, 0, MAP_READ, MAP_FLAG_DO_NOT_WAIT,
                                       nullptr, mapped);
        if (!mapped.pData)
            throw std::runtime_error("View image staging map failed");
        const auto row = std::size_t(desc.Width) * 4;
        const bool valid =
            mapped.Stride >= row &&
            mapped.Stride <= std::numeric_limits<std::size_t>::max() / std::max(1u, desc.Height);
        if (valid)
            for (unsigned y = 0; y < desc.Height; ++y)
                std::memcpy(result.rgba.data() + y * row,
                            static_cast<const std::uint8_t*>(mapped.pData) + y * mapped.Stride,
                            row);
        context->UnmapTextureSubresource(readback_staging_, 0, 0);
        if (!valid)
            throw std::runtime_error("View image staging row layout is invalid");
    } catch (...) {
        clear_readback();
        throw;
    }
    clear_readback();
    return result;
}
} // namespace forge
