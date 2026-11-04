#include "texture_image.hpp"
#include "view_image_limits.h"
#include <stdexcept>

namespace forge {
Rgba8Pixels texture_stored_pixels(const TextureData& texture, TextureImageSlice slice,
                                  std::stop_token stop) {
    const auto cancelled = [&] {
        if (stop.stop_requested())
            throw std::runtime_error("Texture image export cancelled");
    };
    cancelled();
    validate_texture(texture, {texture_image_source_limit});
    const auto format = texture_format_info(texture.format);
    if (texture.format != TextureFormat::R8 && texture.format != TextureFormat::RG8 &&
        texture.format != TextureFormat::RGBA8 && texture.format != TextureFormat::RGBA8Srgb)
        throw std::runtime_error(
            "Stored pixel export requires uncompressed R8, RG8, RGBA8 or RGBA8 sRGB storage");
    const unsigned faces = texture.dimension == TextureDimension::Cube ||
                                   texture.dimension == TextureDimension::CubeArray
                               ? 6u
                               : 1u;
    if (slice.mip >= texture.mips || slice.layer >= texture.layers || slice.face >= faces)
        throw std::runtime_error("Texture image mip, layer or cube face is out of range");
    const auto layout = texture_layout(texture, slice.mip);
    if (slice.depth >= layout.depth)
        throw std::runtime_error("Texture image depth slice is out of range");
    const auto bytes = std::uint64_t(layout.width) * layout.height * 4;
    if (layout.width > FORGE_VIEW_IMAGE_MAX_EXTENT || layout.height > FORGE_VIEW_IMAGE_MAX_EXTENT ||
        bytes > FORGE_VIEW_IMAGE_PIXEL_LIMIT)
        throw std::runtime_error(
            "Texture image exceeds 4096 pixels per axis or 64 MiB; choose a smaller mip");
    const auto index = (std::size_t(slice.layer) * faces + slice.face) * texture.mips + slice.mip;
    const auto& source = texture.subresources.at(index);
    Rgba8Pixels result{layout.width, layout.height, std::vector<std::uint8_t>(std::size_t(bytes)),
                       format.srgb};
    for (unsigned y = 0; y < layout.height; ++y) {
        cancelled();
        const auto* row = source.data() + std::size_t(slice.depth) * layout.slice_bytes +
                          std::size_t(y) * layout.row_bytes;
        auto* output = result.rgba.data() + std::size_t(y) * layout.width * 4;
        for (unsigned x = 0; x < layout.width; ++x) {
            for (unsigned channel = 0; channel < format.channels; ++channel)
                output[x * 4 + channel] =
                    std::to_integer<std::uint8_t>(row[x * format.channels + channel]);
            if (format.channels < 4)
                output[x * 4 + 3] = 255;
        }
    }
    cancelled();
    return result;
}
} // namespace forge
