#pragma once
#include "image_pixels.hpp"
#include <cstddef>
#include <cstdint>
#include <forge/texture_asset.hpp>
#include <stop_token>

namespace forge {
inline constexpr std::size_t texture_image_source_limit = 128ull * 1024 * 1024;
inline constexpr std::size_t texture_hdr_source_limit = 256ull * 1024 * 1024;
inline constexpr std::size_t texture_uint16_source_limit = 256ull * 1024 * 1024;
struct TextureImageSlice {
    unsigned mip = 0, layer = 0, face = 0, depth = 0;
};
// Stored normalized byte channels, top row first. No color/alpha conversion:
// RGBA remains exact; missing G/B are zero and missing A is 255. Image files do
// not carry the Texture's semantic, sampler, alpha convention or mip metadata.
Rgba8Pixels texture_stored_pixels(const TextureData&, TextureImageSlice = {}, std::stop_token = {});
inline bool texture_uint16_storage(TextureFormat format) {
    return format == TextureFormat::R16 || format == TextureFormat::RG16 ||
           format == TextureFormat::RGBA16;
}
// R16/RG16/RGBA16 only. Exact normalized samples; missing G/B=0,A=65535.
Rgba16Pixels texture_stored_uint16_pixels(const TextureData&, TextureImageSlice = {},
                                          std::stop_token = {});
// HdrColor RGBA16Float/RGBA32Float only. Present alpha must be exactly one;
// RGB is finite/nonnegative and remains linear, without exposure or tone mapping.
Rgb32Pixels texture_hdr_pixels(const TextureData&, TextureImageSlice = {}, std::stop_token = {});
} // namespace forge
