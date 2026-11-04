#pragma once
#include "image_pixels.hpp"
#include <forge/texture_asset.hpp>
#include <stop_token>

namespace forge::asset_detail {
inline bool texture_block_image_supported(TextureFormat format) noexcept {
    switch (format) {
    case TextureFormat::BC1:
    case TextureFormat::BC1Srgb:
    case TextureFormat::BC2:
    case TextureFormat::BC2Srgb:
    case TextureFormat::BC3:
    case TextureFormat::BC3Srgb:
    case TextureFormat::BC4:
    case TextureFormat::BC5:
    case TextureFormat::BC7:
    case TextureFormat::BC7Srgb:
        return true;
    default:
        return false;
    }
}
// Validates the bounded 2D plane before allocation. Native codec calls belong
// only to the asset worker and direct codec tests; no container parsing here.
Rgba8Pixels decode_texture_block_image(TextureFormat, unsigned width, unsigned height,
                                       std::span<const std::byte>, std::stop_token = {});
} // namespace forge::asset_detail
