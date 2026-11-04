#include "texture_decode.h"
#include "texture_encode.h"
#include "view_image.hpp"
#include "view_image_limits.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
#include <webp/decode.h>
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
struct Encoded : ForgeImageBytes {
    Encoded() : ForgeImageBytes{} {}
    ~Encoded() { forge_image_bytes_free(this); }
};
void roundtrip(unsigned width, unsigned height, unsigned padding) {
    const auto stride = std::size_t(width) * 4 + padding;
    std::vector<std::uint8_t> source((height - 1) * stride + width * 4, 0x7f);
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x) {
            const std::array<std::uint8_t, 4> pixel{
                std::uint8_t(x * 37 + y * 11), std::uint8_t(x * 13 + y * 71),
                std::uint8_t(x * 29 + y * 19), std::uint8_t(((x + y) % 4) * 85)};
            std::memcpy(source.data() + y * stride + x * 4, pixel.data(), 4);
        }
    Encoded image;
    check(forge_webp_encode_rgba8(source.data(), source.size(), width, height, stride,
                                  FORGE_VIEW_IMAGE_ENCODED_LIMIT, &image),
          image.error);
    int decoded_width = 0, decoded_height = 0;
    check(WebPGetInfo(image.bytes, image.size, &decoded_width, &decoded_height) &&
              decoded_width == int(width) && decoded_height == int(height),
          "Encoded WebP lost its native dimensions");
    std::vector<std::uint8_t> decoded(std::size_t(width) * height * 4);
    check(
        WebPDecodeRGBAInto(image.bytes, image.size, decoded.data(), decoded.size(), int(width * 4)),
        "Native WebP decoder rejected the exported image");
    for (unsigned y = 0; y < height; ++y)
        check(!std::memcmp(source.data() + y * stride, decoded.data() + y * width * 4, width * 4),
              "Lossless WebP changed row order, alpha or transparent RGB");
}
void admission() {
    const std::array<std::uint8_t, 8> pixels{23, 42, 67, 0, 1, 2, 3, 255};
    auto reject = [&](const void* data, std::size_t size, unsigned width, unsigned height,
                      std::size_t stride, std::size_t limit) {
        Encoded encoded;
        check(!forge_webp_encode_rgba8(data, size, width, height, stride, limit, &encoded) &&
                  !encoded.bytes && !encoded.size && encoded.error[0],
              "Invalid WebP admission retained output or omitted its error");
    };
    reject(nullptr, 8, 2, 1, 8, 100);
    reject(pixels.data(), 8, 0, 1, 8, 100);
    reject(pixels.data(), 8, 2, 0, 8, 100);
    reject(pixels.data(), 8, FORGE_VIEW_IMAGE_MAX_EXTENT + 1, 1, 8, 100);
    reject(pixels.data(), 8, 2, 1, 7, 100);
    reject(pixels.data(), 7, 2, 1, 8, 100);
    reject(pixels.data(), 8, 2, 1, std::numeric_limits<std::size_t>::max(), 100);
    reject(pixels.data(), 8, 2, 2, 8, 100);
    reject(pixels.data(), FORGE_VIEW_IMAGE_PIXEL_LIMIT + 1, 2, 1, 8, 100);
    reject(pixels.data(), 8, 2, 1, 8, 0);
    reject(pixels.data(), 8, 2, 1, 8, FORGE_VIEW_IMAGE_ENCODED_LIMIT + 1);
    reject(pixels.data(), 8, 2, 1, 8, 8); // Fail the native writer, not extent admission.
    check(!forge_webp_encode_rgba8(pixels.data(), 8, 2, 1, 8, 100, nullptr),
          "WebP encoding accepted a missing output owner");
}
void files(const std::filesystem::path& root) {
    std::filesystem::create_directories(root);
    const auto path = root / "exact-alpha.WEbP";
    std::vector<std::uint8_t> pixels(4 * 3 * 4);
    for (std::size_t i = 0; i < pixels.size(); ++i)
        pixels[i] = std::uint8_t(i * 17);
    pixels[3] = 0; // Preserve nonzero RGB beneath fully transparent alpha.
    auto bytes = [](const std::filesystem::path& file) {
        std::ifstream input(file, std::ios::binary);
        return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input),
                                         std::istreambuf_iterator<char>());
    };
    auto rejects = [](auto&& operation) {
        try {
            operation();
        } catch (const std::exception&) {
            return;
        }
        throw std::runtime_error("Invalid image file operation succeeded");
    };
    check(forge::view_image_format(path) == forge::ViewImageFormat::Webp &&
              std::strcmp(forge::view_image_mime(forge::ViewImageFormat::Webp), "image/webp") ==
                  0 &&
              std::strcmp(forge::view_image_suffix(forge::ViewImageFormat::Webp), ".webp") == 0,
          "Case-insensitive output admission or WebP metadata disagrees");
    forge::save_view_image(path, 4, 3, pixels);
    const auto encoded = bytes(path);
    check(encoded.size() >= 12 && std::memcmp(encoded.data(), "RIFF", 4) == 0,
          "The .webp output contains another file format");
    const auto preview = forge::read_view_preview(path);
    check(preview.source_width == 4 && preview.source_height == 3 && preview.width == 4 &&
              preview.height == 3,
          "WebP protocol preview changed the source extent");
    struct Decoded : ForgeImagePixels {
        Decoded() : ForgeImagePixels{} {}
        ~Decoded() { forge_image_pixels_free(this); }
    } decoded;
    check(forge_png_decode(preview.png.data(), preview.png.size(), 4096, 64 * 1024 * 1024, 0,
                           &decoded) &&
              decoded.size == pixels.size() &&
              std::memcmp(decoded.pixels, pixels.data(), pixels.size()) == 0,
          "WebP readback did not return an exact RGBA PNG protocol preview");
    rejects([&] { forge::save_view_image(path, 5, 3, pixels); });
    check(bytes(path) == encoded, "Invalid pixels replaced the existing image");
    const auto unsupported = root / "preserved.tif";
    {
        std::ofstream file(unsupported);
        file << "keep previous file";
    }
    const auto previous = bytes(unsupported);
    rejects([&] { forge::save_view_image(unsupported, 4, 3, pixels); });
    check(bytes(unsupported) == previous, "Unsupported format changed an existing file");
    rejects([&] { (void)forge::view_image_format(root / "missing-suffix"); });
    const auto folder = root / "directory.webp";
    std::filesystem::create_directories(folder);
    rejects([&] { forge::save_view_image(folder, 4, 3, pixels); });
    const auto malformed = root / "malformed.webp";
    {
        std::ofstream file(malformed, std::ios::binary);
        file << "RIFF0000WEBPbroken";
    }
    rejects([&] { (void)forge::read_view_preview(malformed); });
    const auto png = root / "original-default.PNG";
    forge::save_view_image(png, 4, 3, pixels);
    check(forge::read_view_preview(png).png == preview.png,
          "PNG/WebP source pixels produced different unchanged-size previews");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need image file fixture folder");
        for (const auto& size : {std::array{1u, 1u}, {1u, 7u}, {7u, 1u}, {3u, 5u}, {61u, 37u}})
            for (unsigned padding : {0u, 3u, 16u})
                roundtrip(size[0], size[1], padding);
        admission();
        files(std::filesystem::absolute(argv[1]));
        std::cout
            << "Native lossless WebP, exact transparent RGBA, padded rows and admission passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
