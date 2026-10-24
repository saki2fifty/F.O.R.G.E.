#include "view_image.hpp"
#include "asset_bytes.hpp"
#include "asset_storage.hpp"
#include "texture_decode.h"
#include "texture_encode.h"
#include "view_image_limits.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace forge {
void save_view_png(const std::filesystem::path& path, unsigned width, unsigned height,
                   std::span<const std::uint8_t> rgba) {
    struct Encoded : ForgeImageBytes {
        Encoded() : ForgeImageBytes{} {}
        ~Encoded() { forge_image_bytes_free(this); }
    } encoded;
    if (!width || !height || width > FORGE_VIEW_IMAGE_MAX_EXTENT ||
        height > FORGE_VIEW_IMAGE_MAX_EXTENT || rgba.size() != std::size_t(width) * height * 4)
        throw std::runtime_error("View image requires complete RGBA8 display pixels");
    if (!forge_png_encode_rgba8(rgba.data(), rgba.size(), width, height, std::size_t(width) * 4,
                                FORGE_VIEW_IMAGE_ENCODED_LIMIT, &encoded))
        throw std::runtime_error(encoded.error);
    asset_storage::replace(
        path, std::string_view(reinterpret_cast<const char*>(encoded.bytes), encoded.size));
}
ViewImagePreview preview_view_pixels(unsigned width, unsigned height,
                                     std::span<const std::uint8_t> rgba) {
    if (!width || !height || width > FORGE_VIEW_IMAGE_MAX_EXTENT ||
        height > FORGE_VIEW_IMAGE_MAX_EXTENT || rgba.size() != std::size_t(width) * height * 4)
        throw std::runtime_error("View preview requires bounded RGBA8 display pixels");
    ViewImagePreview result;
    result.source_width = width;
    result.source_height = height;
    const auto longest = std::max(width, height);
    result.width =
        longest > 1024 ? std::max(1u, unsigned(std::uint64_t(width) * 1024 / longest)) : width;
    result.height =
        longest > 1024 ? std::max(1u, unsigned(std::uint64_t(height) * 1024 / longest)) : height;
    std::vector<std::uint8_t> scaled;
    const auto* pixels = rgba.data();
    if (result.width != width || result.height != height) {
        scaled.resize(std::size_t(result.width) * result.height * 4);
        for (unsigned y = 0; y < result.height; ++y)
            for (unsigned x = 0; x < result.width; ++x) {
                const auto sx =
                    std::clamp((x + .5) * width / result.width - .5, 0., double(width - 1));
                const auto sy =
                    std::clamp((y + .5) * height / result.height - .5, 0., double(height - 1));
                const auto x0 = unsigned(sx), y0 = unsigned(sy), x1 = std::min(x0 + 1, width - 1),
                           y1 = std::min(y0 + 1, height - 1);
                const auto fx = sx - x0, fy = sy - y0;
                for (unsigned channel = 0; channel < 4; ++channel) {
                    const auto at = [&](unsigned px, unsigned py) {
                        return rgba[(std::size_t(py) * width + px) * 4 + channel];
                    };
                    const auto a = at(x0, y0) * (1 - fx) + at(x1, y0) * fx;
                    const auto b = at(x0, y1) * (1 - fx) + at(x1, y1) * fx;
                    scaled[(std::size_t(y) * result.width + x) * 4 + channel] =
                        std::uint8_t(std::lround(a * (1 - fy) + b * fy));
                }
            }
        pixels = scaled.data();
    }
    struct Encoded : ForgeImageBytes {
        Encoded() : ForgeImageBytes{} {}
        ~Encoded() { forge_image_bytes_free(this); }
    } encoded;
    if (!forge_png_encode_rgba8(pixels, std::size_t(result.width) * result.height * 4, result.width,
                                result.height, std::size_t(result.width) * 4, 8 * 1024 * 1024,
                                &encoded))
        throw std::runtime_error(encoded.error);
    const auto* first = reinterpret_cast<const std::byte*>(encoded.bytes);
    result.png.assign(first, first + encoded.size);
    return result;
}
ViewImagePreview read_view_preview(const std::filesystem::path& path) {
    asset_storage::ordinary(path);
    const auto bytes = asset_detail::read_bytes(path, FORGE_VIEW_IMAGE_ENCODED_LIMIT);
    struct Decoded : ForgeImagePixels {
        Decoded() : ForgeImagePixels{} {}
        ~Decoded() { forge_image_pixels_free(this); }
    } image;
    if (!forge_png_decode(bytes.data(), bytes.size(), FORGE_VIEW_IMAGE_MAX_EXTENT,
                          FORGE_VIEW_IMAGE_PIXEL_LIMIT, 0, &image))
        throw std::runtime_error(image.error);
    if (image.channels != 4 || image.component_bytes != 1 ||
        image.stride != std::size_t(image.width) * 4)
        throw std::runtime_error("The owned view artifact is not RGBA8 PNG");
    return preview_view_pixels(image.width, image.height, {image.pixels, image.size});
}
} // namespace forge
