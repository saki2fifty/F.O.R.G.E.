#include "texture_decode.h"
#include "texture_encode.h"
#include "view_image.hpp"
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
struct Encoded : ForgeImageBytes {
    Encoded() : ForgeImageBytes{} {}
    ~Encoded() { forge_image_bytes_free(this); }
};
struct Decoded : ForgeImagePixels {
    Decoded() : ForgeImagePixels{} {}
    ~Decoded() { forge_image_pixels_free(this); }
};
unsigned be32(const unsigned char* p) {
    return (unsigned(p[0]) << 24) | (unsigned(p[1]) << 16) | (unsigned(p[2]) << 8) | p[3];
}
void roundtrip(unsigned width, unsigned height, unsigned padding) {
    const std::size_t stride = width * 4 + padding;
    const std::size_t extent = (height - 1) * stride + width * 4;
    std::vector<unsigned char> input(extent, 0xcd);
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x) {
            const std::array<unsigned char, 4> pixel{
                static_cast<unsigned char>((x * 73 + y * 17) % 256),
                static_cast<unsigned char>((x * 11 + y * 59) % 256),
                static_cast<unsigned char>((x * 31 + y * 7) % 256),
                static_cast<unsigned char>((x + y) % 4 == 0   ? 0
                                           : (x + y) % 4 == 1 ? 128
                                                              : 255)};
            std::memcpy(input.data() + y * stride + x * 4, pixel.data(), 4);
        }
    Encoded encoded;
    check(forge_png_encode_rgba8(input.data(), input.size(), width, height, stride,
                                 68 * 1024 * 1024, &encoded),
          encoded.error);
    check(encoded.size > 32 && be32(encoded.bytes + 16) == width &&
              be32(encoded.bytes + 20) == height && encoded.bytes[24] == 8 &&
              encoded.bytes[25] == 6,
          "PNG header changed dimensions, depth or RGBA channels");
    bool srgb = false;
    for (std::size_t offset = 8; offset + 12 <= encoded.size;) {
        const auto length = be32(encoded.bytes + offset);
        check(length <= encoded.size - offset - 12, "PNG chunk overflows output");
        if (std::memcmp(encoded.bytes + offset + 4, "sRGB", 4) == 0)
            srgb = length == 1 && encoded.bytes[offset + 8] == 0;
        offset += length + 12;
    }
    check(srgb, "Display PNG lacks explicit sRGB intent");
    Decoded decoded;
    check(forge_png_decode(encoded.bytes, encoded.size, 4096, 64 * 1024 * 1024, false, &decoded),
          decoded.error);
    check(decoded.width == width && decoded.height == height && decoded.channels == 4 &&
              decoded.component_bytes == 1 && decoded.stride == width * 4,
          "PNG decode changed image layout");
    for (unsigned y = 0; y < height; ++y)
        check(std::memcmp(input.data() + y * stride, decoded.pixels + y * decoded.stride,
                          width * 4) == 0,
              "PNG changed channel values, alpha, row order or display encoding");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Expected PNG file test directory");
        roundtrip(7, 3, 20);
        roundtrip(67, 9, 5);
        roundtrip(4096, 1, 0);
        roundtrip(1, 4096, 0);
        unsigned char pixel[8] = {0, 127, 255, 64, 255, 128, 0, 0};
        auto reject = [&](const void* input, std::size_t extent, unsigned width, unsigned height,
                          std::size_t stride, std::size_t limit) {
            Encoded result;
            check(!forge_png_encode_rgba8(input, extent, width, height, stride, limit, &result) &&
                      result.bytes == nullptr && result.size == 0 && result.error[0],
                  "Invalid encoding input/budget returned partial output");
        };
        reject(nullptr, 4, 1, 1, 4, 1024);
        reject(pixel, 4, 0, 1, 4, 1024);
        reject(pixel, 4, 1, 0, 4, 1024);
        reject(pixel, 4, 4097, 1, 4097 * 4, 1024);
        reject(pixel, 4, 1, 4097, 4, 1024);
        reject(pixel, 7, 1, 2, 4, 1024);
        reject(pixel, 8, 1, 2, 3, 1024);
        reject(pixel, 8, 1, 2, std::numeric_limits<std::size_t>::max(), 1024);
        reject(pixel, 64 * 1024 * 1024 + 1, 1, 1, 4, 1024);
        reject(pixel, 4, 1, 1, 4, 0);
        reject(pixel, 4, 1, 1, 4, 68 * 1024 * 1024 + 1);
        reject(pixel, 4, 1, 1, 4, 8); // libpng callback failure after signature/partial output.
        check(!forge_png_encode_rgba8(pixel, 4, 1, 1, 4, 1024, nullptr), "Null output accepted");
        const auto root = std::filesystem::absolute(argv[1]).lexically_normal();
        std::filesystem::create_directories(root);
        const auto path = root / std::filesystem::u8path("view-\xc3\xa4.png");
        auto read_file = [](const std::filesystem::path& file) {
            std::ifstream input(file, std::ios::binary);
            check(bool(input), "Saved PNG could not be opened");
            return std::vector<unsigned char>(std::istreambuf_iterator<char>(input), {});
        };
        forge::save_view_png(path, 1, 1, std::span(pixel, 4));
        const auto previous = read_file(path);
        Decoded file_pixels;
        check(forge_png_decode(previous.data(), previous.size(), 4096, 1024, false, &file_pixels) &&
                  file_pixels.size == 4 && std::memcmp(file_pixels.pixels, pixel, 4) == 0,
              "Atomic image save changed detached display pixels");
        bool refused = false;
        try {
            forge::save_view_png(path, 2, 1, std::span(pixel, 4));
        } catch (const std::exception&) {
            refused = true;
        }
        check(refused && read_file(path) == previous, "Invalid image replaced previous output");
        refused = false;
        try {
            forge::save_view_png(root, 1, 1, std::span(pixel, 4));
        } catch (const std::exception&) {
            refused = true;
        }
        check(refused && read_file(path) == previous, "Failed file publication changed output");
        for (const auto& entry : std::filesystem::directory_iterator(root))
            check(entry.path().extension() != ".pending", "Failed PNG save left staging files");
        const auto redirected = root / "redirect.png";
        std::error_code link_error;
        std::filesystem::remove(redirected, link_error);
        std::filesystem::create_symlink(path, redirected, link_error);
        if (!link_error) {
            refused = false;
            try {
                forge::save_view_png(redirected, 1, 1, std::span(pixel + 4, 4));
            } catch (const std::exception&) {
                refused = true;
            }
            check(refused && read_file(path) == previous, "PNG publication followed a redirect");
            std::filesystem::remove(redirected);
        } else
            std::cout << "Symlink publication case unavailable on this host\n";
        forge::save_view_png(path, 1, 1, std::span(pixel + 4, 4));
        check(read_file(path) != previous, "Explicit PNG replacement did not publish");
        std::cout << "PNG display image roundtrip, stride, alpha and limits passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
