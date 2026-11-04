#include "asset_bytes.hpp"
#include "texture_decode.h"
#include "texture_encode.h"
#include "texture_image.hpp"
#include "view_image.hpp"
#include <array>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <zlib.h>
using namespace forge;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void rejects(F&& operation, std::string_view reason = {}) {
    try {
        operation();
    } catch (const std::exception& error) {
        check(reason.empty() ||
                  std::string_view(error.what()).find(reason) != std::string_view::npos,
              "Normalized16-bit request rejected for an unexpected reason");
        return;
    }
    throw std::runtime_error("Invalid normalized16-bit request succeeded");
}
struct Decoded : ForgeImagePixels {
    Decoded() : ForgeImagePixels{} {}
    ~Decoded() { forge_image_pixels_free(this); }
};
struct Encoded : ForgeImageBytes {
    Encoded() : ForgeImageBytes{} {}
    ~Encoded() { forge_image_bytes_free(this); }
};
void roundtrip(unsigned width, unsigned height, unsigned padding) {
    const auto stride = std::size_t(width) * 8 + padding;
    std::vector<unsigned char> input((height - 1) * stride + width * 8, 0xcd);
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x)
            for (unsigned c = 0; c < 4; ++c) {
                const auto value = std::uint16_t(x * 997 + y * 257 + c * 16383);
                std::memcpy(input.data() + y * stride + x * 8 + c * 2, &value, 2);
            }
    Encoded encoded;
    check(forge_png_encode_rgba16(input.data(), input.size(), width, height, stride,
                                  FORGE_UINT16_IMAGE_ENCODED_LIMIT, &encoded),
          encoded.error);
    check(encoded.size > 26 && encoded.bytes[24] == 16 && encoded.bytes[25] == 6,
          "PNG file lost16-bit RGBA precision");
    Decoded decoded;
    check(forge_png_decode(encoded.bytes, encoded.size, 4096, FORGE_UINT16_IMAGE_PIXEL_LIMIT, 0,
                           &decoded),
          decoded.error);
    check(decoded.width == width && decoded.height == height && decoded.channels == 4 &&
              decoded.component_bytes == 2 && decoded.stride == width * 8 && !decoded.srgb,
          "PNG16 roundtrip changed layout or invented an sRGB label");
    for (unsigned y = 0; y < height; ++y)
        check(std::memcmp(input.data() + y * stride, decoded.pixels + y * decoded.stride,
                          width * 8) == 0,
              "PNG16 lost low bits, swapped sample byte order or changed row order");
}
void network_order() {
    // One pixel has no left/up predictor under any valid PNG filter. Decompress
    // IDAT directly, independently of the PNG decoder's own endian transform.
    const std::array<std::uint16_t, 4> input{0x0102, 0x80ff, 0xff00, 0x00ff};
    Encoded png;
    check(forge_png_encode_rgba16(input.data(), sizeof(input), 1, 1, 8,
                                  FORGE_UINT16_IMAGE_ENCODED_LIMIT, &png),
          png.error);
    auto be32 = [](const unsigned char* p) {
        return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
               (std::uint32_t(p[2]) << 8) | p[3];
    };
    std::vector<unsigned char> compressed;
    for (std::size_t at = 8; at + 12 <= png.size;) {
        const auto length = be32(png.bytes + at);
        check(length <= png.size - at - 12, "PNG16 chunk extent is invalid");
        if (std::memcmp(png.bytes + at + 4, "IDAT", 4) == 0)
            compressed.insert(compressed.end(), png.bytes + at + 8, png.bytes + at + 8 + length);
        at += std::size_t(length) + 12;
    }
    std::array<unsigned char, 9> raw{};
    uLongf size = static_cast<uLongf>(raw.size());
    check(uncompress(raw.data(), &size, compressed.data(), static_cast<uLong>(compressed.size())) ==
                  Z_OK &&
              size == raw.size() && raw[0] <= 4,
          "PNG16 IDAT has invalid framing/filter");
    const std::array<unsigned char, 8> expected{1, 2, 128, 255, 255, 0, 0, 255};
    check(std::memcmp(raw.data() + 1, expected.data(), expected.size()) == 0,
          "PNG16 wire samples are not high-byte-first independent of the decoder");
}
void admission() {
    const std::array<std::uint16_t, 8> samples{0, 1, 255, 256, 257, 32768, 65534, 65535};
    auto reject = [&](const void* p, std::size_t size, unsigned width, unsigned height,
                      std::size_t stride, std::size_t limit) {
        Encoded encoded;
        check(!forge_png_encode_rgba16(p, size, width, height, stride, limit, &encoded) &&
                  !encoded.bytes && !encoded.size && encoded.error[0],
              "Invalid PNG16 returned partial output or omitted its error");
    };
    reject(nullptr, 16, 2, 1, 16, 1000);
    reject(samples.data(), 16, 0, 1, 16, 1000);
    reject(samples.data(), 16, 2, 0, 16, 1000);
    reject(samples.data(), 16, 4097, 1, 16, 1000);
    reject(samples.data(), 16, 1, 4097, 16, 1000);
    reject(samples.data(), 15, 2, 1, 16, 1000);
    reject(samples.data(), 16, 2, 1, 15, 1000);
    reject(samples.data(), 16, 2, 2, std::numeric_limits<std::size_t>::max(), 1000);
    reject(samples.data(), FORGE_UINT16_IMAGE_PIXEL_LIMIT + 1, 2, 1, 16, 1000);
    reject(samples.data(), 16, 2, 1, 16, 0);
    reject(samples.data(), 16, 2, 1, 16, FORGE_UINT16_IMAGE_ENCODED_LIMIT + 1);
    reject(samples.data(), 16, 2, 1, 16, 8); // Native callback after PNG signature.
    check(!forge_png_encode_rgba16(samples.data(), 16, 2, 1, 16, 1000, nullptr),
          "PNG16 accepted missing output ownership");
}
void projection() {
    TextureData source;
    source.width = 2;
    source.height = 1;
    source.format = TextureFormat::RGBA16;
    source.semantic = TextureSemantic::Data;
    const std::vector<std::uint16_t> expected{0, 1, 255, 256, 257, 32768, 65534, 65535};
    std::vector<std::byte> packed;
    for (auto value : expected) {
        packed.push_back(std::byte(value & 255));
        packed.push_back(std::byte(value >> 8));
    }
    source.subresources = {packed};
    check(texture_stored_uint16_pixels(source).rgba == expected,
          "Normalized16-bit cooked little-endian samples were reinterpreted");
    source.format = TextureFormat::R16;
    source.subresources = {{std::byte{1}, std::byte{0}, std::byte{254}, std::byte{255}}};
    check(texture_stored_uint16_pixels(source).rgba ==
              std::vector<std::uint16_t>{1, 0, 0, 65535, 65534, 0, 0, 65535},
          "R16 expansion changed red precision or missing channels");
    source.format = TextureFormat::RG16;
    source.subresources = {{std::byte{1}, std::byte{0}, std::byte{2}, std::byte{1}, std::byte{3},
                            std::byte{2}, std::byte{255}, std::byte{255}}};
    check(texture_stored_uint16_pixels(source).rgba ==
              std::vector<std::uint16_t>{1, 258, 0, 65535, 515, 65535, 0, 65535},
          "RG16 treated green as alpha or lost precision");
    std::stop_source stop;
    stop.request_stop();
    rejects([&] { (void)texture_stored_uint16_pixels(source, {}, stop.get_token()); });
    rejects([&] { (void)texture_stored_uint16_pixels(source, {1}); });
    rejects([&] { (void)texture_stored_uint16_pixels(source, {0, 1}); });
    rejects([&] { (void)texture_stored_uint16_pixels(source, {0, 0, 1}); });
    rejects([&] { (void)texture_stored_uint16_pixels(source, {0, 0, 0, 1}); });
    TextureData cube;
    cube.dimension = TextureDimension::CubeArray;
    cube.width = cube.height = 2;
    cube.layers = 2;
    cube.mips = 2;
    cube.format = TextureFormat::R16;
    cube.semantic = TextureSemantic::Data;
    for (unsigned i = 0; i < 24; ++i) {
        const auto value = std::uint16_t(500 + i);
        std::vector<std::byte> level;
        for (unsigned p = 0; p < (i % 2 ? 1 : 4); ++p) {
            level.push_back(std::byte(value & 255));
            level.push_back(std::byte(value >> 8));
        }
        cube.subresources.push_back(std::move(level));
    }
    check(texture_stored_uint16_pixels(cube, {1, 1, 4}).rgba ==
              std::vector<std::uint16_t>{521, 0, 0, 65535},
          "PNG16 cube-array mip/layer/face selected the wrong subresource");
    TextureData volume;
    volume.dimension = TextureDimension::D3;
    volume.width = 2;
    volume.height = 1;
    volume.depth = 2;
    volume.format = TextureFormat::R16;
    volume.semantic = TextureSemantic::Data;
    volume.subresources = {{std::byte{1}, std::byte{0}, std::byte{2}, std::byte{0}, std::byte{3},
                            std::byte{0}, std::byte{4}, std::byte{0}}};
    check(texture_stored_uint16_pixels(volume, {0, 0, 0, 1}).rgba ==
              std::vector<std::uint16_t>{3, 0, 0, 65535, 4, 0, 0, 65535},
          "PNG16 depth slice read the wrong plane");
    TextureData oversized;
    oversized.dimension = TextureDimension::D2Array;
    oversized.width = oversized.height = 4096;
    oversized.layers = 3;
    oversized.format = TextureFormat::RGBA16;
    rejects([&] { (void)texture_stored_uint16_pixels(oversized); }, "payload exceeds budget");
    oversized.dimension = TextureDimension::D2;
    oversized.layers = 1;
    oversized.width = 8192;
    oversized.height = 1;
    oversized.format = TextureFormat::R16;
    oversized.subresources = {std::vector<std::byte>(8192 * 2)};
    rejects([&] { (void)texture_stored_uint16_pixels(oversized); }, "4096");
    source.format = TextureFormat::RGBA16Float;
    source.subresources = {std::vector<std::byte>(16)}; // Finite half values, not invalid NaNs.
    rejects([&] { (void)texture_stored_uint16_pixels(source); }, "requires R16");
}
void files(const std::filesystem::path& root) {
    std::filesystem::create_directories(root);
    const auto path = root / "precision.PNG";
    Rgba16Pixels image{3, 1, {0, 1, 255, 0, 256, 257, 32768, 32767, 65534, 65535, 129, 65535}};
    save_image_pixels(path, image);
    const auto good = asset_detail::read_bytes(path, FORGE_UINT16_IMAGE_ENCODED_LIMIT);
    const auto preview = preview_image_pixels(ImagePixels{image});
    check(read_view_preview(path).png == preview.png,
          "Owned-file PNG16 preview disagrees with the detached image preview");
    Decoded decoded;
    check(forge_png_decode(preview.png.data(), preview.png.size(), 4096,
                           FORGE_VIEW_IMAGE_PIXEL_LIMIT, 0, &decoded) &&
              decoded.component_bytes == 1 && decoded.channels == 4 && !decoded.srgb,
          "PNG16 inspection preview is not unlabelled8-bit RGBA");
    const std::vector<std::uint8_t> expected{0, 0, 1, 0, 1, 1, 128, 127, 255, 255, 1, 255};
    check(decoded.size == expected.size() &&
              std::memcmp(decoded.pixels, expected.data(), expected.size()) == 0,
          "PNG16 preview changed quantization, low/high endpoints, alpha or hidden RGB");
    for (const char* suffix : {".webp", ".jpg", ".hdr"})
        rejects([&] { save_image_pixels(root / (std::string("precision") + suffix), image); });
    image.rgba.push_back(1);
    rejects([&] { save_image_pixels(path, image); });
    rejects([&] { (void)preview_image_pixels(image); });
    check(asset_detail::read_bytes(path, FORGE_UINT16_IMAGE_ENCODED_LIMIT) == good,
          "Failed PNG16 export changed the last good file");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need PNG16 fixture output folder");
        for (const auto& size :
             {std::array{1u, 1u}, {3u, 5u}, {17u, 19u}, {4096u, 1u}, {1u, 4096u}})
            for (unsigned padding : {0u, 1u, 7u})
                roundtrip(size[0], size[1], padding);
        network_order();
        admission();
        projection();
        files(std::filesystem::absolute(argv[1]));
        std::cout
            << "Exact normalized16-bit projection/PNG, byte order, previews and limits passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
