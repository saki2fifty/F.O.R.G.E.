#include "texture_decode.h"
#include "texture_encode.h"
#include "view_image.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
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
template <class F> void rejects(F&& operation) {
    try {
        operation();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid JPEG request succeeded");
}
std::vector<unsigned char> bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    check(bool(input), "Missing JPEG fixture output");
    return {std::istreambuf_iterator<char>(input), {}};
}
forge::Rgba8Pixels pattern(unsigned width, unsigned height) {
    forge::Rgba8Pixels result{width, height, std::vector<std::uint8_t>(width * height * 4, 255)};
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x) {
            auto* p = result.rgba.data() + (std::size_t(y) * width + x) * 4;
            p[0] = std::uint8_t(x * 17 + y * 11);
            p[1] = std::uint8_t(x * 7 + y * 23);
            p[2] = std::uint8_t(x * 31 + y * 13);
        }
    return result;
}
double encode(unsigned width, unsigned height, int quality) {
    const auto input = pattern(width, height);
    Encoded encoded;
    check(forge_jpeg_encode_rgba8(input.rgba.data(), input.rgba.size(), width, height, width * 4,
                                  quality, FORGE_VIEW_IMAGE_ENCODED_LIMIT, &encoded),
          encoded.error);
    check(encoded.size > 4 && encoded.bytes[0] == 255 && encoded.bytes[1] == 216 &&
              encoded.bytes[encoded.size - 2] == 255 && encoded.bytes[encoded.size - 1] == 217,
          "Encoded JPEG framing is incomplete");
    Decoded decoded;
    check(forge_jpeg_decode(encoded.bytes, encoded.size, 4096, FORGE_VIEW_IMAGE_PIXEL_LIMIT, 0,
                            &decoded),
          decoded.error);
    check(decoded.width == width && decoded.height == height && decoded.channels == 3 &&
              decoded.component_bytes == 1 && decoded.stride == width * 3,
          "JPEG decode changed dimensions or RGB layout");
    double error = 0;
    for (std::size_t p = 0; p < std::size_t(width) * height; ++p)
        for (unsigned c = 0; c < 3; ++c) {
            const auto difference = double(decoded.pixels[p * 3 + c]) - input.rgba[p * 4 + c];
            error += difference * difference;
        }
    return error / (double(width) * height * 3);
}
void admission() {
    auto input = pattern(2, 1);
    auto reject = [&](const void* data, std::size_t size, unsigned width, unsigned height,
                      std::size_t stride, int quality, std::size_t limit) {
        Encoded encoded;
        check(
            !forge_jpeg_encode_rgba8(data, size, width, height, stride, quality, limit, &encoded) &&
                !encoded.bytes && !encoded.size && encoded.error[0],
            "Rejected JPEG retained partial bytes or omitted its error");
    };
    const auto* p = input.rgba.data();
    reject(nullptr, 8, 2, 1, 8, 90, 1024);
    reject(p, 8, 0, 1, 8, 90, 1024);
    reject(p, 8, 2, 0, 8, 90, 1024);
    reject(p, 8, 4097, 1, 8, 90, 1024);
    reject(p, 8, 1, 4097, 8, 90, 1024);
    reject(p, 7, 2, 1, 8, 90, 1024);
    reject(p, 8, 2, 1, 9, 90, 1024);
    reject(p, 8, 2, 1, std::numeric_limits<std::size_t>::max(), 90, 1024);
    reject(p, FORGE_VIEW_IMAGE_PIXEL_LIMIT + 1, 2, 1, 8, 90, 1024);
    for (int quality : {-1, 0, 101, std::numeric_limits<int>::max()})
        reject(p, 8, 2, 1, 8, quality, 1024);
    reject(p, 8, 2, 1, 8, 90, 0);
    reject(p, 8, 2, 1, 8, 90, FORGE_VIEW_IMAGE_ENCODED_LIMIT + 1);
    // The native callback keeps running after its first failure; no partial
    // allocation or subsequent append may escape this bounded output failure.
    for (std::size_t limit : {1u, 8u, 128u, 600u})
        reject(p, 8, 2, 1, 8, 90, limit);
    input.rgba[3] = 254;
    reject(p, 8, 2, 1, 8, 90, 1024);
    input.rgba[3] = 0;
    reject(p, 8, 2, 1, 8, 90, 1024);
    check(!forge_jpeg_encode_rgba8(p, 8, 2, 1, 8, 90, 1024, nullptr),
          "JPEG accepted missing result ownership");
}
void files(const std::filesystem::path& root) {
    std::filesystem::create_directories(root);
    const auto path = root / "picture.JpEg";
    check(forge::view_image_format(path) == forge::ViewImageFormat::Jpeg &&
              std::strcmp(forge::view_image_mime(forge::ViewImageFormat::Jpeg), "image/jpeg") ==
                  0 &&
              std::strcmp(forge::view_image_suffix(forge::ViewImageFormat::Jpeg), ".jpg") == 0 &&
              forge::view_image_format("picture.JPG") == forge::ViewImageFormat::Jpeg,
          "JPEG suffix or MIME disagrees");
    auto input = pattern(33, 19);
    forge::save_image_pixels(path, input, {100});
    const auto good = bytes(path);
    Decoded jpeg;
    check(forge_jpeg_decode(good.data(), good.size(), 4096, FORGE_VIEW_IMAGE_PIXEL_LIMIT, 0, &jpeg),
          jpeg.error);
    const auto renamed = root / "header-only.bin";
    std::filesystem::copy_file(path, renamed, std::filesystem::copy_options::overwrite_existing);
    const auto preview = forge::read_view_preview(renamed);
    check(preview.source_width == input.width && preview.source_height == input.height &&
              preview.width == input.width && preview.height == input.height,
          "JPEG preview changed the source dimensions");
    Decoded png;
    check(forge_png_decode(preview.png.data(), preview.png.size(), 4096,
                           FORGE_VIEW_IMAGE_PIXEL_LIMIT, 0, &png) &&
              png.channels == 4 && png.srgb,
          "JPEG preview is not opaque sRGB RGBA PNG");
    bool lossy = false;
    for (std::size_t p = 0; p < input.rgba.size() / 4; ++p) {
        for (unsigned c = 0; c < 3; ++c) {
            check(png.pixels[p * 4 + c] == jpeg.pixels[p * 3 + c],
                  "JPEG preview substituted input pixels for decoded output");
            lossy |= png.pixels[p * 4 + c] != input.rgba[p * 4 + c];
        }
        check(png.pixels[p * 4 + 3] == 255, "JPEG preview added transparency");
    }
    check(lossy, "Quality100 was incorrectly treated as lossless");
    rejects([&] { forge::save_image_pixels(path, input, {0}); });
    rejects([&] { forge::save_image_pixels(path, input, {101}); });
    input.srgb = false;
    rejects([&] { forge::save_image_pixels(path, input); });
    input.srgb = true;
    input.rgba[3] = 0;
    rejects([&] { forge::save_image_pixels(path, input); });
    input.rgba[3] = 255;
    input.rgba.pop_back();
    rejects([&] { forge::save_image_pixels(path, input); });
    check(bytes(path) == good, "A failed JPEG write replaced the last good file");
    const auto malformed = root / "truncated.jpg";
    {
        std::ofstream output(malformed, std::ios::binary);
        output.write(reinterpret_cast<const char*>(good.data()), std::streamsize(good.size() - 2));
    }
    rejects([&] { (void)forge::read_view_preview(malformed); });
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need JPEG output fixture folder");
        for (const auto& size :
             {std::array{1u, 1u}, {1u, 17u}, {19u, 1u}, {17u, 19u}, {4096u, 1u}, {1u, 4096u}})
            for (int quality : {1, 50, 90, 100})
                (void)encode(size[0], size[1], quality);
        check(encode(63, 47, 100) < encode(63, 47, 10),
              "Higher quality did not improve this fixture's decoded fidelity");
        admission();
        files(std::filesystem::absolute(argv[1]));
        std::cout << "Bounded opaque JPEG, quality, decoded previews and file retention passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
