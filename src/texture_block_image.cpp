#include "texture_block_image.hpp"
#include "view_image_limits.h"
#include <algorithm>
#include <array>
#include <basisu_gpu_texture.h>
#include <cstring>
#include <stdexcept>

namespace forge::asset_detail {
namespace {
void cancelled(std::stop_token stop) {
    if (stop.stop_requested())
        throw std::runtime_error("Compressed Texture image decode cancelled");
}
// BC2/BC3 RGB always uses four colors, regardless of endpoint order. Normalize
// a local copy for the native BC1 primitive: exchanging endpoints exchanges
// palette entries 0/1 and 2/3. Equal endpoints need only palette entry zero.
void four_color(std::array<std::uint8_t, 16>& block, basisu::color_rgba* pixels) {
    auto* color = block.data() + 8;
    const unsigned first = unsigned(color[0]) | (unsigned(color[1]) << 8);
    const unsigned second = unsigned(color[2]) | (unsigned(color[3]) << 8);
    if (first < second) {
        std::swap(color[0], color[2]);
        std::swap(color[1], color[3]);
        for (unsigned i = 4; i < 8; ++i)
            color[i] ^= 0x55;
    } else if (first == second)
        std::fill(color + 4, color + 8, std::uint8_t{0});
    (void)basisu::unpack_bc1(color, pixels, false);
}
} // namespace
Rgba8Pixels decode_texture_block_image(TextureFormat format, unsigned width, unsigned height,
                                       std::span<const std::byte> source, std::stop_token stop) {
    cancelled(stop);
    if (!texture_block_image_supported(format))
        throw std::runtime_error(
            "Compressed image export supports UNORM BC1, BC2, BC3, BC4, BC5 and BC7 only");
    if (!width || !height || width > FORGE_VIEW_IMAGE_MAX_EXTENT ||
        height > FORGE_VIEW_IMAGE_MAX_EXTENT)
        throw std::runtime_error("Compressed Texture image axes must be between 1 and 4096");
    const auto info = texture_format_info(format);
    const unsigned columns = (width + 3) / 4, rows = (height + 3) / 4;
    if (source.size() != std::size_t(columns) * rows * info.block_bytes)
        throw std::runtime_error("Compressed Texture image block extent does not match its plane");
    const auto output_bytes = std::size_t(width) * height * 4;
    if (output_bytes > FORGE_VIEW_IMAGE_PIXEL_LIMIT)
        throw std::runtime_error("Compressed Texture image exceeds 64 MiB");
    Rgba8Pixels result{width, height, std::vector<std::uint8_t>(output_bytes), info.srgb, true};
    for (unsigned row = 0; row < rows; ++row) {
        cancelled(stop);
        for (unsigned column = 0; column < columns; ++column) {
            alignas(16) std::array<std::uint8_t, 16> block{};
            std::memcpy(block.data(),
                        source.data() + (std::size_t(row) * columns + column) * info.block_bytes,
                        info.block_bytes);
            std::array<basisu::color_rgba, 16> pixels;
            for (auto& pixel : pixels)
                pixel.set(0, 0, 0, 255);
            switch (format) {
            case TextureFormat::BC1:
            case TextureFormat::BC1Srgb:
                // The native bool identifies punch-through, not decode failure.
                (void)basisu::unpack_bc1(block.data(), pixels.data(), true);
                break;
            case TextureFormat::BC2:
            case TextureFormat::BC2Srgb:
                four_color(block, pixels.data());
                for (unsigned i = 0; i < 16; ++i)
                    pixels[i].a = std::uint8_t(((block[i / 2] >> (4 * (i % 2))) & 15) * 17);
                break;
            case TextureFormat::BC3:
            case TextureFormat::BC3Srgb:
                four_color(block, pixels.data());
                basisu::unpack_bc4(block.data(), &pixels[0].a, sizeof(basisu::color_rgba));
                break;
            case TextureFormat::BC4:
                basisu::unpack_bc4(block.data(), &pixels[0].r, sizeof(basisu::color_rgba));
                break;
            case TextureFormat::BC5:
                basisu::unpack_bc5(block.data(), pixels.data());
                break;
            case TextureFormat::BC7:
            case TextureFormat::BC7Srgb:
                if (!basisu::unpack_bc7(block.data(), pixels.data()))
                    throw std::runtime_error("Invalid BC7 block in selected Texture image");
                break;
            default:
                throw std::runtime_error("Unsupported compressed Texture image format");
            }
            static_assert(sizeof(basisu::color_rgba) == 4);
            const unsigned copied = std::min(4u, width - column * 4);
            for (unsigned y = 0; y < 4 && row * 4 + y < height; ++y)
                std::memcpy(result.rgba.data() +
                                (std::size_t(row * 4 + y) * width + column * 4) * 4,
                            pixels.data() + y * 4, copied * 4);
        }
    }
    cancelled(stop);
    return result;
}
} // namespace forge::asset_detail
