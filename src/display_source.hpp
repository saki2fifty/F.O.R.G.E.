#pragma once
#include "Graphics/GraphicsEngine/interface/Texture.h"
#include <algorithm>
#include <array>
#include <stdexcept>
namespace forge {
// Shared admission for display and its optional exposure meter. These consumers
// interpret the same single-sample linear HDR mip; no asset/resource ownership.
inline std::array<unsigned, 2> display_source_extent(Diligent::ITextureView* source) {
    using namespace Diligent;
    if (!source)
        throw std::runtime_error("Display resolve requires an HDR source");
    const auto& input = source->GetTexture()->GetDesc();
    if (input.Type != RESOURCE_DIM_TEX_2D || input.SampleCount != 1 ||
        (input.Format != TEX_FORMAT_RGBA16_FLOAT && input.Format != TEX_FORMAT_RGBA32_FLOAT))
        throw std::runtime_error(
            "Display resolve requires a single-sample linear floating-point RGBA target");
    const auto& view = source->GetDesc();
    if (view.ViewType != TEXTURE_VIEW_SHADER_RESOURCE || view.MostDetailedMip >= input.MipLevels ||
        view.MostDetailedMip >= 32)
        throw std::runtime_error("Display resolve requires a valid shader-resource mip view");
    return {std::max(1u, input.Width >> view.MostDetailedMip),
            std::max(1u, input.Height >> view.MostDetailedMip)};
}
} // namespace forge
