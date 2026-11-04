#include "texture_image.hpp"
#include "view_image_limits.h"
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace forge {
namespace {
void cancelled(std::stop_token stop) {
    if (stop.stop_requested())
        throw std::runtime_error("Texture image export cancelled");
}
struct ImageSlice {
    TextureLayout layout;
    std::span<const std::byte> bytes;
};
ImageSlice image_slice(const TextureData& texture, TextureImageSlice slice,
                       std::size_t source_limit) {
    validate_texture(texture, {source_limit});
    const unsigned faces = texture.dimension == TextureDimension::Cube ||
                                   texture.dimension == TextureDimension::CubeArray
                               ? 6u
                               : 1u;
    if (slice.mip >= texture.mips || slice.layer >= texture.layers || slice.face >= faces)
        throw std::runtime_error("Texture image mip, layer or cube face is out of range");
    const auto layout = texture_layout(texture, slice.mip);
    if (slice.depth >= layout.depth)
        throw std::runtime_error("Texture image depth slice is out of range");
    if (layout.width > FORGE_VIEW_IMAGE_MAX_EXTENT || layout.height > FORGE_VIEW_IMAGE_MAX_EXTENT)
        throw std::runtime_error(
            "Texture image exceeds 4096 pixels per axis; choose a smaller mip");
    const auto index = (std::size_t(slice.layer) * faces + slice.face) * texture.mips + slice.mip;
    return {layout,
            std::span(texture.subresources.at(index))
                .subspan(std::size_t(slice.depth) * layout.slice_bytes, layout.slice_bytes)};
}
float scalar(const std::byte* bytes, unsigned bits) {
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
    std::uint32_t value = 0;
    for (unsigned i = 0; i < bits / 8; ++i)
        value |= std::to_integer<std::uint32_t>(bytes[i]) << (8 * i);
    if (bits == 32)
        return std::bit_cast<float>(value);
    const auto exponent = (value >> 10) & 31, fraction = value & 1023;
    if (exponent == 31)
        throw std::runtime_error("HDR image samples must be finite");
    // IEEE binary16: subnormals have no implicit leading one. Both cases
    // convert exactly to binary32; sign is applied after the magnitude.
    const float magnitude = exponent ? std::ldexp(float(1024 + fraction), int(exponent) - 25)
                                     : std::ldexp(float(fraction), -24);
    return value & 0x8000 ? -magnitude : magnitude;
}
} // namespace
Rgba8Pixels texture_stored_pixels(const TextureData& texture, TextureImageSlice slice,
                                  std::stop_token stop) {
    cancelled(stop);
    const auto selected = image_slice(texture, slice, texture_image_source_limit);
    const auto format = texture_format_info(texture.format);
    if (texture.format != TextureFormat::R8 && texture.format != TextureFormat::RG8 &&
        texture.format != TextureFormat::RGBA8 && texture.format != TextureFormat::RGBA8Srgb)
        throw std::runtime_error(
            "Stored pixel export requires uncompressed R8, RG8, RGBA8 or RGBA8 sRGB storage");
    const auto& layout = selected.layout;
    const auto bytes = std::uint64_t(layout.width) * layout.height * 4;
    if (bytes > FORGE_VIEW_IMAGE_PIXEL_LIMIT)
        throw std::runtime_error("Texture image exceeds 64 MiB; choose a smaller mip");
    Rgba8Pixels result{layout.width, layout.height, std::vector<std::uint8_t>(std::size_t(bytes)),
                       format.srgb};
    for (unsigned y = 0; y < layout.height; ++y) {
        cancelled(stop);
        const auto* row = selected.bytes.data() + std::size_t(y) * layout.row_bytes;
        auto* output = result.rgba.data() + std::size_t(y) * layout.width * 4;
        for (unsigned x = 0; x < layout.width; ++x) {
            for (unsigned channel = 0; channel < format.channels; ++channel)
                output[x * 4 + channel] =
                    std::to_integer<std::uint8_t>(row[x * format.channels + channel]);
            if (format.channels < 4)
                output[x * 4 + 3] = 255;
        }
    }
    cancelled(stop);
    return result;
}
Rgba16Pixels texture_stored_uint16_pixels(const TextureData& texture, TextureImageSlice slice,
                                          std::stop_token stop) {
    cancelled(stop);
    const auto selected = image_slice(texture, slice, texture_uint16_source_limit);
    if (!texture_uint16_storage(texture.format))
        throw std::runtime_error("Normalized16-bit export requires R16, RG16 or RGBA16 storage");
    const auto format = texture_format_info(texture.format);
    const auto& layout = selected.layout;
    const auto count = std::uint64_t(layout.width) * layout.height * 4;
    if (count * sizeof(std::uint16_t) > FORGE_UINT16_IMAGE_PIXEL_LIMIT)
        throw std::runtime_error("Normalized16-bit image exceeds128 MiB; choose a smaller mip");
    Rgba16Pixels result{layout.width, layout.height,
                        std::vector<std::uint16_t>(std::size_t(count))};
    for (unsigned y = 0; y < layout.height; ++y) {
        cancelled(stop);
        const auto* row = selected.bytes.data() + std::size_t(y) * layout.row_bytes;
        auto* output = result.rgba.data() + std::size_t(y) * layout.width * 4;
        for (unsigned x = 0; x < layout.width; ++x) {
            for (unsigned c = 0; c < format.channels; ++c) {
                const auto* source = row + (std::size_t(x) * format.channels + c) * 2;
                output[x * 4 + c] = std::uint16_t(std::to_integer<unsigned>(source[0]) |
                                                  (std::to_integer<unsigned>(source[1]) << 8));
            }
            if (format.channels < 4)
                output[x * 4 + 3] = 65535;
        }
    }
    cancelled(stop);
    return result;
}
Rgb32Pixels texture_hdr_pixels(const TextureData& texture, TextureImageSlice slice,
                               std::stop_token stop) {
    cancelled(stop);
    const auto selected = image_slice(texture, slice, texture_hdr_source_limit);
    if (texture.semantic != TextureSemantic::HdrColor ||
        (texture.format != TextureFormat::RGBA16Float &&
         texture.format != TextureFormat::RGBA32Float))
        throw std::runtime_error("HDR output requires HdrColor RGBA16Float or RGBA32Float storage");
    const auto& layout = selected.layout;
    const auto samples = std::uint64_t(layout.width) * layout.height * 3;
    if (samples * sizeof(float) > FORGE_HDR_IMAGE_PIXEL_LIMIT)
        throw std::runtime_error("HDR RGB image exceeds 192 MiB; choose a smaller mip");
    const auto bits = texture_format_info(texture.format).float_bits;
    const unsigned stride = bits / 8;
    Rgb32Pixels result{layout.width, layout.height, std::vector<float>(std::size_t(samples))};
    for (unsigned y = 0; y < layout.height; ++y) {
        cancelled(stop);
        const auto* row = selected.bytes.data() + std::size_t(y) * layout.row_bytes;
        for (unsigned x = 0; x < layout.width; ++x) {
            const auto* pixel = row + std::size_t(x) * stride * 4;
            if (scalar(pixel + 3 * stride, bits) != 1)
                throw std::runtime_error(
                    "HDR image output requires every selected alpha to be one");
            for (unsigned c = 0; c < 3; ++c) {
                const float value = scalar(pixel + c * stride, bits);
                if (!std::isfinite(value) || value < 0)
                    throw std::runtime_error("HDR image RGB must be finite and nonnegative");
                result.rgb[(std::size_t(y) * layout.width + x) * 3 + c] = value;
            }
        }
    }
    cancelled(stop);
    return result;
}
} // namespace forge
