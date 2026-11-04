#include "view_image.hpp"
#include "asset_bytes.hpp"
#include "asset_storage.hpp"
#include "texture_decode.h"
#include "texture_encode.h"
#include "view_image_hdr.hpp"
#include "view_image_limits.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
namespace forge {
void ImageEncodingOptions::validate() const {
    if (jpeg_quality < FORGE_JPEG_QUALITY_MIN || jpeg_quality > FORGE_JPEG_QUALITY_MAX)
        throw std::runtime_error("JPEG quality must be between 1 and 100");
}
ViewImageFormat view_image_format(const std::filesystem::path& path) {
    auto extension = path.extension().u8string();
    for (auto& c : extension)
        if (c >= u8'A' && c <= u8'Z')
            c = char8_t(c + (u8'a' - u8'A'));
    if (extension == u8".png")
        return ViewImageFormat::Png;
    if (extension == u8".webp")
        return ViewImageFormat::Webp;
    if (extension == u8".hdr")
        return ViewImageFormat::Hdr;
    if (extension == u8".jpg" || extension == u8".jpeg")
        return ViewImageFormat::Jpeg;
    throw std::runtime_error(
        "Choose a PNG (.png), lossless WebP (.webp), JPEG (.jpg/.jpeg) or HDR (.hdr) filename");
}
const char* view_image_suffix(ViewImageFormat format) {
    switch (format) {
    case ViewImageFormat::Png:
        return ".png";
    case ViewImageFormat::Webp:
        return ".webp";
    case ViewImageFormat::Hdr:
        return ".hdr";
    case ViewImageFormat::Jpeg:
        return ".jpg";
    }
    throw std::runtime_error("Unsupported view image format");
}
const char* view_image_mime(ViewImageFormat format) {
    switch (format) {
    case ViewImageFormat::Png:
        return "image/png";
    case ViewImageFormat::Webp:
        return "image/webp";
    case ViewImageFormat::Hdr:
        return "image/vnd.radiance";
    case ViewImageFormat::Jpeg:
        return "image/jpeg";
    }
    throw std::runtime_error("Unsupported view image format");
}
namespace {
void save_image(const std::filesystem::path& path, unsigned width, unsigned height,
                std::span<const std::uint8_t> rgba, ViewImageFormat format, bool srgb = true,
                const ImageEncodingOptions& options = {}) {
    options.validate();
    struct Encoded : ForgeImageBytes {
        Encoded() : ForgeImageBytes{} {}
        ~Encoded() { forge_image_bytes_free(this); }
    } encoded;
    if (!width || !height || width > FORGE_VIEW_IMAGE_MAX_EXTENT ||
        height > FORGE_VIEW_IMAGE_MAX_EXTENT || rgba.size() != std::size_t(width) * height * 4)
        throw std::runtime_error("View image requires complete RGBA8 display pixels");
    if (format == ViewImageFormat::Hdr)
        throw std::runtime_error("HDR output requires opaque linear HDR pixels");
    if ((format == ViewImageFormat::Webp || format == ViewImageFormat::Jpeg) && !srgb)
        throw std::runtime_error("Stored channels without an sRGB label require PNG output");
    const auto ok = format == ViewImageFormat::Png
                        ? forge_png_encode_rgba8_profile(rgba.data(), rgba.size(), width, height,
                                                         std::size_t(width) * 4, srgb,
                                                         FORGE_VIEW_IMAGE_ENCODED_LIMIT, &encoded)
                    : format == ViewImageFormat::Jpeg
                        ? forge_jpeg_encode_rgba8(rgba.data(), rgba.size(), width, height,
                                                  std::size_t(width) * 4, options.jpeg_quality,
                                                  FORGE_VIEW_IMAGE_ENCODED_LIMIT, &encoded)
                        : forge_webp_encode_rgba8(rgba.data(), rgba.size(), width, height,
                                                  std::size_t(width) * 4,
                                                  FORGE_VIEW_IMAGE_ENCODED_LIMIT, &encoded);
    if (!ok)
        throw std::runtime_error(encoded.error);
    asset_storage::replace(
        path, std::string_view(reinterpret_cast<const char*>(encoded.bytes), encoded.size));
}
} // namespace
void save_view_image(const std::filesystem::path& path, unsigned width, unsigned height,
                     std::span<const std::uint8_t> rgba, const ImageEncodingOptions& options) {
    save_image(path, width, height, rgba, view_image_format(path), true, options);
}
void save_image_pixels(const std::filesystem::path& path, const Rgba8Pixels& pixels,
                       const ImageEncodingOptions& options) {
    save_image(path, pixels.width, pixels.height, pixels.rgba, view_image_format(path), pixels.srgb,
               options);
}
void save_image_pixels(const std::filesystem::path& path, const Rgba16Pixels& pixels,
                       const ImageEncodingOptions& options) {
    options.validate();
    if (view_image_format(path) != ViewImageFormat::Png)
        throw std::runtime_error("Normalized16-bit samples require PNG (.png) output");
    if (!pixels.width || !pixels.height || pixels.width > FORGE_VIEW_IMAGE_MAX_EXTENT ||
        pixels.height > FORGE_VIEW_IMAGE_MAX_EXTENT ||
        pixels.rgba.size() != std::size_t(pixels.width) * pixels.height * 4)
        throw std::runtime_error("Normalized16-bit PNG requires complete packed RGBA samples");
    struct Encoded : ForgeImageBytes {
        Encoded() : ForgeImageBytes{} {}
        ~Encoded() { forge_image_bytes_free(this); }
    } encoded;
    if (!forge_png_encode_rgba16(pixels.rgba.data(), pixels.rgba.size() * sizeof(std::uint16_t),
                                 pixels.width, pixels.height, std::size_t(pixels.width) * 8,
                                 FORGE_UINT16_IMAGE_ENCODED_LIMIT, &encoded))
        throw std::runtime_error(encoded.error);
    asset_storage::replace(
        path, std::string_view(reinterpret_cast<const char*>(encoded.bytes), encoded.size));
}
void save_image_pixels(const std::filesystem::path& path, const Rgb32Pixels& pixels,
                       const ImageEncodingOptions& options) {
    options.validate();
    if (view_image_format(path) != ViewImageFormat::Hdr)
        throw std::runtime_error("Linear HDR pixels require an HDR (.hdr) destination");
    const auto bytes = encode_hdr_image(pixels);
    asset_storage::replace(
        path, std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}
void save_image_pixels(const std::filesystem::path& path, const ImagePixels& pixels,
                       const ImageEncodingOptions& options) {
    std::visit([&](const auto& value) { save_image_pixels(path, value, options); }, pixels);
}
void save_view_png(const std::filesystem::path& path, unsigned width, unsigned height,
                   std::span<const std::uint8_t> rgba) {
    save_image(path, width, height, rgba, ViewImageFormat::Png);
}
ViewImagePreview preview_view_pixels(unsigned width, unsigned height,
                                     std::span<const std::uint8_t> rgba, bool srgb) {
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
    if (!forge_png_encode_rgba8_profile(pixels, std::size_t(result.width) * result.height * 4,
                                        result.width, result.height, std::size_t(result.width) * 4,
                                        srgb, 8 * 1024 * 1024, &encoded))
        throw std::runtime_error(encoded.error);
    const auto* first = reinterpret_cast<const std::byte*>(encoded.bytes);
    result.png.assign(first, first + encoded.size);
    return result;
}
ViewImagePreview preview_image_pixels(const Rgba8Pixels& pixels) {
    return preview_view_pixels(pixels.width, pixels.height, pixels.rgba, pixels.srgb);
}
namespace {
ViewImagePreview preview_uint16_pixels(unsigned width, unsigned height,
                                       std::span<const std::byte> native, bool srgb = false) {
    if (!width || !height || width > FORGE_VIEW_IMAGE_MAX_EXTENT ||
        height > FORGE_VIEW_IMAGE_MAX_EXTENT || native.size() != std::size_t(width) * height * 8 ||
        native.size() > FORGE_UINT16_IMAGE_PIXEL_LIMIT)
        throw std::runtime_error("Normalized16-bit preview needs complete bounded RGBA samples");
    std::vector<std::uint8_t> rgba(native.size() / 2);
    for (std::size_t at = 0; at < rgba.size(); ++at) {
        std::uint16_t value;
        std::memcpy(&value, native.data() + at * 2, sizeof(value));
        rgba[at] = std::uint8_t((std::uint32_t(value) * 255 + 32767) / 65535);
    }
    return preview_view_pixels(width, height, rgba, srgb);
}
} // namespace
ViewImagePreview preview_image_pixels(const Rgba16Pixels& pixels) {
    return preview_uint16_pixels(pixels.width, pixels.height,
                                 std::as_bytes(std::span(pixels.rgba)));
}
ViewImagePreview preview_image_pixels(const ImagePixels& pixels) {
    if (const auto* rgba = std::get_if<Rgba8Pixels>(&pixels))
        return preview_image_pixels(*rgba);
    if (const auto* rgba = std::get_if<Rgba16Pixels>(&pixels))
        return preview_image_pixels(*rgba);
    return preview_hdr_image(encode_hdr_image(std::get<Rgb32Pixels>(pixels)));
}
ViewImagePreview read_view_preview(const std::filesystem::path& path) {
    asset_storage::ordinary(path);
    const auto bytes = asset_detail::read_bytes(path, FORGE_UINT16_IMAGE_ENCODED_LIMIT);
    if ((bytes.size() >= 10 && std::memcmp(bytes.data(), "#?RADIANCE", 10) == 0) ||
        (bytes.size() >= 6 && std::memcmp(bytes.data(), "#?RGBE", 6) == 0))
        return preview_hdr_image(bytes);
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
    const bool jpeg = bytes.size() >= 2 && bytes[0] == std::byte{255} && bytes[1] == std::byte{216};
    if ((webp || jpeg) && bytes.size() > FORGE_VIEW_IMAGE_ENCODED_LIMIT)
        throw std::runtime_error("Byte image artifact exceeds68 MiB encoded budget");
    const auto decode = webp ? forge_webp_decode : jpeg ? forge_jpeg_decode : forge_png_decode;
    if (!decode(bytes.data(), bytes.size(), FORGE_VIEW_IMAGE_MAX_EXTENT,
                webp   ? FORGE_VIEW_IMAGE_ENCODED_LIMIT
                : jpeg ? FORGE_VIEW_IMAGE_PIXEL_LIMIT
                       : FORGE_UINT16_IMAGE_PIXEL_LIMIT,
                0, &image))
        throw std::runtime_error(image.error);
    if (jpeg) {
        if ((image.channels != 1 && image.channels != 3) || image.component_bytes != 1 ||
            image.stride != std::size_t(image.width) * image.channels ||
            image.size != image.stride * image.height)
            throw std::runtime_error("The owned JPEG artifact is not bounded gray/RGB bytes");
        std::vector<std::uint8_t> rgba(std::size_t(image.width) * image.height * 4, 255);
        for (std::size_t at = 0; at < rgba.size() / 4; ++at)
            for (unsigned c = 0; c < 3; ++c)
                rgba[at * 4 + c] =
                    image.pixels[at * image.channels + (image.channels == 1 ? 0 : c)];
        return preview_view_pixels(image.width, image.height, rgba, true);
    }
    if (!webp && image.channels == 4 && image.component_bytes == 2 &&
        image.stride == std::size_t(image.width) * 8 && image.size == image.stride * image.height)
        return preview_uint16_pixels(image.width, image.height,
                                     std::as_bytes(std::span(image.pixels, image.size)),
                                     image.srgb != 0);
    if (bytes.size() > FORGE_VIEW_IMAGE_ENCODED_LIMIT)
        throw std::runtime_error("Byte image artifact exceeds68 MiB encoded budget");
    if (image.channels != 4 || image.component_bytes != 1 ||
        image.stride != std::size_t(image.width) * 4 || image.size > FORGE_VIEW_IMAGE_PIXEL_LIMIT)
        throw std::runtime_error("The owned view artifact is not RGBA8 PNG or WebP");
    return preview_view_pixels(image.width, image.height, {image.pixels, image.size},
                               image.srgb != 0);
}
} // namespace forge
