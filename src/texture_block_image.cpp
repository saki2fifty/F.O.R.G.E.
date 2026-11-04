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
std::array<unsigned, 2> admit_plane(TextureFormat format, unsigned width, unsigned height,
                                    std::span<const std::byte> source, unsigned sample_bytes) {
    if (!width || !height || width > FORGE_VIEW_IMAGE_MAX_EXTENT ||
        height > FORGE_VIEW_IMAGE_MAX_EXTENT)
        throw std::runtime_error("Compressed Texture image axes must be between 1 and 4096");
    const unsigned columns = (width + 3) / 4, rows = (height + 3) / 4;
    if (source.size() != std::size_t(columns) * rows * texture_format_info(format).block_bytes)
        throw std::runtime_error("Compressed Texture image block extent does not match its plane");
    const auto limit =
        sample_bytes == 2 ? FORGE_UINT16_IMAGE_PIXEL_LIMIT : FORGE_VIEW_IMAGE_PIXEL_LIMIT;
    if (std::size_t(width) * height * 4 * sample_bytes > limit)
        throw std::runtime_error("Compressed Texture image exceeds its pixel storage budget");
    return {columns, rows};
}
std::array<std::uint16_t, 16> signed_channel(const std::byte* block) {
    const auto endpoint = [](std::byte byte) {
        const auto value = std::to_integer<unsigned>(byte);
        return value < 128 ? int(value) : int(value) - 256;
    };
    const int first = endpoint(block[0]), second = endpoint(block[1]);
    if (first == -127 && second == -128)
        throw std::runtime_error(
            "Ambiguous signed block endpoints -127/-128; re-encode the Texture");
    const int a = std::max(-127, first), b = std::max(-127, second);
    // Quantize the normalized signed rational once, after its fixed [-1,1] bias.
    // Integer arithmetic makes half-up rounding independent of the host float mode.
    const auto mapped = [](int numerator, int divisor) {
        const auto shifted = std::int64_t(numerator) + 127 * divisor;
        return std::uint16_t((shifted * 65535 + 127 * divisor) / (254 * divisor));
    };
    std::array<std::uint16_t, 8> palette{mapped(a, 1), mapped(b, 1)};
    palette[6] = 0;
    palette[7] = 65535;
    const int divisor = first > second ? 7 : 5;
    for (int code = 2; code < divisor + 1; ++code)
        palette[code] = mapped((divisor + 1 - code) * a + (code - 1) * b, divisor);
    std::uint64_t selectors = 0;
    for (unsigned byte = 0; byte < 6; ++byte)
        selectors |= std::uint64_t(std::to_integer<unsigned>(block[byte + 2])) << (8 * byte);
    std::array<std::uint16_t, 16> result;
    for (unsigned pixel = 0; pixel < result.size(); ++pixel)
        result[pixel] = palette[(selectors >> (3 * pixel)) & 7];
    return result;
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
    if (!texture_unorm_block_image_supported(format))
        throw std::runtime_error(
            "Compressed image export supports UNORM BC1, BC2, BC3, BC4, BC5 and BC7 only");
    const auto [columns, rows] = admit_plane(format, width, height, source, 1);
    const auto info = texture_format_info(format);
    const auto output_bytes = std::size_t(width) * height * 4;
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
Rgba16Pixels decode_texture_signed_block_image(TextureFormat format, unsigned width,
                                               unsigned height, std::span<const std::byte> source,
                                               std::stop_token stop) {
    cancelled(stop);
    if (!texture_snorm_block_storage(format))
        throw std::runtime_error("Signed block image export supports BC4 SNORM and BC5 SNORM only");
    const auto [columns, rows] = admit_plane(format, width, height, source, 2);
    const unsigned block_bytes = texture_format_info(format).block_bytes;
    Rgba16Pixels result{width, height, std::vector<std::uint16_t>(std::size_t(width) * height * 4),
                        true};
    for (unsigned row = 0; row < rows; ++row) {
        cancelled(stop);
        for (unsigned column = 0; column < columns; ++column) {
            const auto* block = source.data() + (std::size_t(row) * columns + column) * block_bytes;
            const auto red = signed_channel(block);
            const auto green = format == TextureFormat::BC5Snorm ? signed_channel(block + 8)
                                                                 : std::array<std::uint16_t, 16>{};
            for (unsigned y = 0; y < 4 && row * 4 + y < height; ++y)
                for (unsigned x = 0; x < 4 && column * 4 + x < width; ++x) {
                    auto* target = result.rgba.data() +
                                   (std::size_t(row * 4 + y) * width + column * 4 + x) * 4;
                    target[0] = red[y * 4 + x];
                    target[1] = green[y * 4 + x];
                    target[3] = 65535;
                }
        }
    }
    cancelled(stop);
    return result;
}
} // namespace forge::asset_detail
