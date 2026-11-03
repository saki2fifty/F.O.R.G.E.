#include "view_image.hpp"
#include "asset_bytes.hpp"
#include "asset_storage.hpp"
#include "texture_decode.h"
#include "texture_encode.h"
#include "view_image_limits.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
namespace forge {
ViewImageFormat view_image_format(const std::filesystem::path& path) {
    auto extension = path.extension().u8string();
    for (auto& c : extension)
        if (c >= u8'A' && c <= u8'Z')
            c = char8_t(c + (u8'a' - u8'A'));
    if (extension == u8".png")
        return ViewImageFormat::Png;
    if (extension == u8".webp")
        return ViewImageFormat::Webp;
    throw std::runtime_error("Choose a PNG (.png) or lossless WebP (.webp) image filename");
}
const char* view_image_suffix(ViewImageFormat format) {
    switch (format) {
    case ViewImageFormat::Png:
        return ".png";
    case ViewImageFormat::Webp:
        return ".webp";
    }
    throw std::runtime_error("Unsupported view image format");
}
const char* view_image_mime(ViewImageFormat format) {
    switch (format) {
    case ViewImageFormat::Png:
        return "image/png";
    case ViewImageFormat::Webp:
        return "image/webp";
    }
    throw std::runtime_error("Unsupported view image format");
}
namespace {
void save_image(const std::filesystem::path& path, unsigned width, unsigned height,
                std::span<const std::uint8_t> rgba, ViewImageFormat format) {
    struct Encoded : ForgeImageBytes {
        Encoded() : ForgeImageBytes{} {}
        ~Encoded() { forge_image_bytes_free(this); }
    } encoded;
    if (!width || !height || width > FORGE_VIEW_IMAGE_MAX_EXTENT ||
        height > FORGE_VIEW_IMAGE_MAX_EXTENT || rgba.size() != std::size_t(width) * height * 4)
        throw std::runtime_error("View image requires complete RGBA8 display pixels");
    const auto encode =
        format == ViewImageFormat::Png ? forge_png_encode_rgba8 : forge_webp_encode_rgba8;
    if (!encode(rgba.data(), rgba.size(), width, height, std::size_t(width) * 4,
                FORGE_VIEW_IMAGE_ENCODED_LIMIT, &encoded))
        throw std::runtime_error(encoded.error);
    asset_storage::replace(
        path, std::string_view(reinterpret_cast<const char*>(encoded.bytes), encoded.size));
}
} // namespace
void save_view_image(const std::filesystem::path& path, unsigned width, unsigned height,
                     std::span<const std::uint8_t> rgba) {
    save_image(path, width, height, rgba, view_image_format(path));
}
void save_view_png(const std::filesystem::path& path, unsigned width, unsigned height,
                   std::span<const std::uint8_t> rgba) {
    save_image(path, width, height, rgba, ViewImageFormat::Png);
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
    const bool webp = bytes.size() >= 12 && std::memcmp(bytes.data(), "RIFF", 4) == 0 &&
                      std::memcmp(bytes.data() + 8, "WEBP", 4) == 0;
    // The common decoder's budget covers input and output. At this extent its
    // RGBA8 output is already bounded by the stricter pixel budget; permit the
    // encoded overhead separately without narrowing valid exported images.
    static_assert(std::uint64_t(FORGE_VIEW_IMAGE_MAX_EXTENT) * FORGE_VIEW_IMAGE_MAX_EXTENT * 4 <=
                  FORGE_VIEW_IMAGE_PIXEL_LIMIT);
    const auto decode = webp ? forge_webp_decode : forge_png_decode;
    if (!decode(bytes.data(), bytes.size(), FORGE_VIEW_IMAGE_MAX_EXTENT,
                webp ? FORGE_VIEW_IMAGE_ENCODED_LIMIT : FORGE_VIEW_IMAGE_PIXEL_LIMIT, 0, &image))
        throw std::runtime_error(image.error);
    if (image.channels != 4 || image.component_bytes != 1 ||
        image.stride != std::size_t(image.width) * 4 || image.size > FORGE_VIEW_IMAGE_PIXEL_LIMIT)
        throw std::runtime_error("The owned view artifact is not RGBA8 PNG or WebP");
    return preview_view_pixels(image.width, image.height, {image.pixels, image.size});
}
} // namespace forge
