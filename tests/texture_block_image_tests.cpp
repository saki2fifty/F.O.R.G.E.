#include "asset_bytes.hpp"
#include "texture_block_image.hpp"
#include "texture_decode.h"
#include "texture_image_process.hpp"
#include "view_image.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

using namespace forge;
using namespace forge::asset_detail;
namespace {
using Block = std::vector<std::byte>;
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void reject(F action, std::string_view reason) {
    try {
        action();
    } catch (const std::exception& error) {
        check(std::string_view(error.what()).find(reason) != std::string_view::npos,
              "Compressed image rejected for an unexpected reason");
        return;
    }
    throw std::runtime_error("Invalid compressed image admitted");
}
Block color(unsigned first, unsigned second, unsigned selectors = 0xe4) {
    Block block(8);
    block[0] = std::byte(first & 255);
    block[1] = std::byte(first >> 8);
    block[2] = std::byte(second & 255);
    block[3] = std::byte(second >> 8);
    std::fill(block.begin() + 4, block.end(), std::byte(selectors));
    return block;
}
Block scalar(unsigned first, unsigned second) {
    Block block(8);
    block[0] = std::byte(first);
    block[1] = std::byte(second);
    std::uint64_t selectors = 0;
    for (unsigned i = 0; i < 16; ++i)
        selectors |= std::uint64_t(i % 8) << (i * 3);
    for (unsigned i = 0; i < 6; ++i)
        block[2 + i] = std::byte((selectors >> (i * 8)) & 255);
    return block;
}
Block joined(Block first, const Block& second) {
    first.insert(first.end(), second.begin(), second.end());
    return first;
}
template <class Image>
void pixel(const Image& image, unsigned index, std::array<unsigned, 4> expected) {
    for (unsigned i = 0; i < 4; ++i)
        check(image.rgba.at(std::size_t(index) * 4 + i) == expected[i],
              "Decoded reference texel differs from the numeric vector");
}
TextureData texture(TextureFormat format, unsigned width, unsigned height, Block block) {
    TextureData result;
    result.format = format;
    result.semantic =
        texture_format_info(format).srgb ? TextureSemantic::Color : TextureSemantic::Data;
    result.width = width;
    result.height = height;
    const auto size = texture_layout(result, 0).bytes;
    Block bytes;
    while (bytes.size() < size)
        bytes.insert(bytes.end(), block.begin(), block.end());
    result.subresources = {std::move(bytes)};
    validate_texture(result);
    return result;
}
Rgba8Pixels unsigned_pixels(const std::filesystem::path& worker, const std::filesystem::path& root,
                            const TextureData& data, TextureImageSlice slice = {},
                            std::stop_token stop = {}) {
    return std::get<Rgba8Pixels>(texture_compressed_pixels(worker, root, data, slice, stop));
}
void numeric() {
    for (const auto format : {TextureFormat::BC1, TextureFormat::BC1Srgb}) {
        const auto image = decode_texture_block_image(format, 4, 4, color(0xf800, 0x001f));
        pixel(image, 0, {255, 0, 0, 255});
        pixel(image, 1, {0, 0, 255, 255});
        pixel(image, 2, {170, 0, 85, 255});
        pixel(image, 3, {85, 0, 170, 255});
        check(image.srgb == (format == TextureFormat::BC1Srgb) && image.block_decoded,
              "Compressed color label or provenance changed");
        const auto punch = decode_texture_block_image(format, 4, 4, color(0x001f, 0xf800));
        pixel(punch, 2, {127, 0, 127, 255});
        pixel(punch, 3, {0, 0, 0, 0});
        const auto equal = decode_texture_block_image(format, 4, 4, color(0xffff, 0xffff));
        pixel(equal, 0, {255, 255, 255, 255});
        pixel(equal, 3, {0, 0, 0, 0});
    }
    Block nibbles(8);
    for (unsigned i = 0; i < 8; ++i)
        nibbles[i] = std::byte((i * 2) | ((i * 2 + 1) << 4));
    for (const auto format :
         {TextureFormat::BC2, TextureFormat::BC2Srgb, TextureFormat::BC3, TextureFormat::BC3Srgb}) {
        const bool bc2 = format == TextureFormat::BC2 || format == TextureFormat::BC2Srgb;
        const auto alpha = bc2 ? nibbles : scalar(255, 0);
        for (const auto& endpoints : {std::pair{0xf800u, 0x001fu}, std::pair{0x001fu, 0xf800u},
                                      std::pair{0xf800u, 0xf800u}}) {
            const auto image = decode_texture_block_image(
                format, 4, 4, joined(alpha, color(endpoints.first, endpoints.second)));
            const bool reversed = endpoints.first < endpoints.second;
            const bool equal = endpoints.first == endpoints.second;
            pixel(image, 2,
                  {equal      ? 255u
                   : reversed ? 85u
                              : 170u,
                   0,
                   equal      ? 0u
                   : reversed ? 170u
                              : 85u,
                   bc2 ? 34u : 218u});
            pixel(image, 3,
                  {equal      ? 255u
                   : reversed ? 170u
                              : 85u,
                   0,
                   equal      ? 0u
                   : reversed ? 85u
                              : 170u,
                   bc2 ? 51u : 182u});
            for (unsigned i = 0; bc2 && i < 16; ++i)
                check(image.rgba[i * 4 + 3] == i * 17, "BC2 alpha nibble expansion changed");
        }
    }
    const std::array<unsigned, 8> high{255, 0, 218, 182, 145, 109, 72, 36};
    const std::array<unsigned, 8> low{10, 110, 30, 50, 70, 90, 0, 255};
    const auto red = decode_texture_block_image(TextureFormat::BC4, 4, 4, scalar(255, 0));
    const auto rg = decode_texture_block_image(TextureFormat::BC5, 4, 4,
                                               joined(scalar(255, 0), scalar(10, 110)));
    const auto alpha = decode_texture_block_image(TextureFormat::BC3, 4, 4,
                                                  joined(scalar(10, 110), color(0xffff, 0)));
    for (unsigned i = 0; i < 16; ++i) {
        pixel(red, i, {high[i % 8], 0, 0, 255});
        pixel(rg, i, {high[i % 8], low[i % 8], 0, 255});
        check(alpha.rgba[i * 4 + 3] == low[i % 8], "BC3 six-entry alpha changed");
    }
    // One legal zero-endpoint vector in every BC7 mode. The mode prefix is
    // unary; modes 0..3 have implicit opaque alpha, modes 4..7 store alpha.
    for (unsigned mode = 0; mode < 8; ++mode) {
        Block block(16);
        block[0] = std::byte(1u << mode);
        for (const auto format : {TextureFormat::BC7, TextureFormat::BC7Srgb}) {
            const auto image = decode_texture_block_image(format, 4, 4, block);
            for (unsigned i = 0; i < 16; ++i)
                pixel(image, i, {0, 0, 0, mode < 4 ? 255u : 0u});
        }
    }
    // Independently packed constant mode-6 endpoints from the format bit layout.
    // Seven channel bits plus each endpoint's P-bit give these odd RGBA values.
    Block mode6(16);
    auto put_bits = [&](unsigned offset, unsigned count, unsigned value) {
        for (unsigned bit = 0; bit < count; ++bit)
            if (value & (1u << bit))
                mode6[(offset + bit) / 8] |= std::byte(1u << ((offset + bit) % 8));
    };
    put_bits(0, 7, 64);
    for (unsigned endpoint = 0; endpoint < 2; ++endpoint) {
        put_bits(7 + endpoint * 7, 7, 61);
        put_bits(21 + endpoint * 7, 7, 30);
        put_bits(35 + endpoint * 7, 7, 115);
        put_bits(49 + endpoint * 7, 7, 42);
        put_bits(63 + endpoint, 1, 1);
    }
    const auto mode6_image = decode_texture_block_image(TextureFormat::BC7Srgb, 4, 4, mode6);
    for (unsigned i = 0; i < 16; ++i)
        pixel(mode6_image, i, {123, 61, 231, 85});
    reject([&] { decode_texture_block_image(TextureFormat::BC7, 4, 4, Block(16)); },
           "Invalid BC7 block");
    const auto blocks = joined(joined(color(0xf800, 0, 0), color(0x07e0, 0, 0)),
                               joined(color(0x001f, 0, 0), color(0xffff, 0, 0)));
    const auto cropped = decode_texture_block_image(TextureFormat::BC1, 7, 5, blocks);
    check(cropped.width == 7 && cropped.height == 5 && cropped.rgba.size() == 140,
          "Odd block edge extent changed");
    pixel(cropped, 0, {255, 0, 0, 255});
    pixel(cropped, 6, {0, 255, 0, 255});
    pixel(cropped, 28, {0, 0, 255, 255});
    pixel(cropped, 34, {255, 255, 255, 255});
    const auto one = decode_texture_block_image(TextureFormat::BC1, 1, 1, color(0xf800, 0));
    check(one.rgba.size() == 4, "Small compressed mip gained padding texels");
    for (const auto format :
         {TextureFormat::BC4Snorm, TextureFormat::BC5Snorm, TextureFormat::BC6Unsigned,
          TextureFormat::BC6Signed, TextureFormat::RGBA8, TextureFormat(999)})
        reject([&] { decode_texture_block_image(format, 1, 1, Block(16)); }, "supports UNORM");
    reject([&] { decode_texture_block_image(TextureFormat::BC1, 0, 4, Block(8)); }, "axes");
    reject([&] { decode_texture_block_image(TextureFormat::BC1, 4097, 4, Block(8)); }, "axes");
    reject([&] { decode_texture_block_image(TextureFormat::BC1, 4, 4, Block(7)); }, "extent");
    reject([&] { decode_texture_block_image(TextureFormat::BC1, 4, 4, Block(9)); }, "extent");
    std::stop_source stopped;
    stopped.request_stop();
    reject(
        [&] {
            decode_texture_block_image(TextureFormat::BC1, 4, 4, Block(8), stopped.get_token());
        },
        "cancelled");
}
void protocol() {
    ImportProcessRequest request{{{"recipe", texture_image_decode_recipe},
                                  {"profile", texture_image_decode_profile},
                                  {"format", unsigned(TextureFormat::BC1)},
                                  {"width", 1u},
                                  {"height", 1u}},
                                 {{"blocks.bin", color(0xf800, 0)}}};
    const auto result = execute_texture_image_recipe(request);
    check(result.size() == 1 && result[0].name == "rgba8.bin" && result[0].bytes.size() == 4 &&
              result[0].bytes[0] == std::byte{255},
          "Worker recipe RGBA8 extent changed");
    for (const auto key : {"width", "height", "format"}) {
        auto invalid = request;
        invalid.payload[key] = -1;
        reject([&] { execute_texture_image_recipe(invalid); }, "integer");
        invalid.payload[key] = 1.5;
        reject([&] { execute_texture_image_recipe(invalid); }, "integer");
    }
    auto invalid = request;
    invalid.payload["profile"] = "unknown";
    reject([&] { execute_texture_image_recipe(invalid); }, "request");
    invalid = request;
    invalid.payload["extra"] = true;
    reject([&] { execute_texture_image_recipe(invalid); }, "request");
    invalid = request;
    invalid.inputs[0].name = "other.bin";
    reject([&] { execute_texture_image_recipe(invalid); }, "request");
}
void signed_numeric() {
    const std::array<unsigned, 8> ascending{0, 65535, 13107, 26214, 39321, 52428, 0, 65535};
    const std::array<unsigned, 8> descending{65535, 0, 56173, 46811, 37449, 28086, 18724, 9362};
    const auto endpoints = joined(scalar(129, 127), scalar(127, 129));
    const auto rg = decode_texture_signed_block_image(TextureFormat::BC5Snorm, 4, 4, endpoints);
    check(rg.snorm_block_decoded, "Signed reference provenance missing");
    for (unsigned i = 0; i < 16; ++i)
        pixel(rg, i, {ascending[i % 8], descending[i % 8], 0, 65535});
    // Every signed endpoint pair and selector: independent normalized floating
    // interpolation oracle, checked against the integer production quantizer.
    for (int first = -128; first <= 127; ++first)
        for (int second = -128; second <= 127; ++second) {
            const auto block = scalar(unsigned(first) & 255, unsigned(second) & 255);
            if (first == -127 && second == -128) {
                reject(
                    [&] {
                        decode_texture_signed_block_image(TextureFormat::BC4Snorm, 4, 4, block);
                    },
                    "Ambiguous");
                continue;
            }
            const auto actual =
                decode_texture_signed_block_image(TextureFormat::BC4Snorm, 4, 4, block);
            const auto a = std::max(-1.L, first / 127.L), b = std::max(-1.L, second / 127.L);
            for (unsigned i = 0; i < 8; ++i) {
                const long double sample = i == 0           ? a
                                           : i == 1         ? b
                                           : first > second ? std::lerp(a, b, (i - 1) / 7.L)
                                           : i < 6          ? std::lerp(a, b, (i - 1) / 5.L)
                                           : i == 6         ? -1.L
                                                            : 1.L;
                // Reference float ties can lie just below a half integer. This
                // epsilon is far below the smallest non-tie rational gap (1/1778).
                const auto expected =
                    unsigned(std::floor((sample + 1) * .5L * 65535 + .5L + 1e-9L));
                if (actual.rgba[i * 4] != expected)
                    throw std::runtime_error("Signed endpoint oracle: " + std::to_string(first) +
                                             "," + std::to_string(second) + " code " +
                                             std::to_string(i) + " actual " +
                                             std::to_string(actual.rgba[i * 4]) + " expected " +
                                             std::to_string(expected));
                pixel(actual, i, {expected, 0, 0, 65535});
            }
        }
    const auto cropped = decode_texture_signed_block_image(
        TextureFormat::BC5Snorm, 7, 5,
        joined(joined(endpoints, endpoints), joined(endpoints, endpoints)));
    check(cropped.rgba.size() == 7 * 5 * 4, "Signed edge blocks retained padding");
    pixel(cropped, 34, {ascending[2], descending[2], 0, 65535});
    reject([&] { decode_texture_signed_block_image(TextureFormat::BC4Snorm, 0, 4, Block(8)); },
           "axes");
    reject([&] { decode_texture_signed_block_image(TextureFormat::BC5Snorm, 4, 4, Block(15)); },
           "extent");
    reject([&] { decode_texture_signed_block_image(TextureFormat::BC6Signed, 1, 1, Block(16)); },
           "BC4 SNORM");
    std::stop_source stopped;
    stopped.request_stop();
    reject(
        [&] {
            decode_texture_signed_block_image(TextureFormat::BC4Snorm, 4, 4, Block(8),
                                              stopped.get_token());
        },
        "cancelled");
    ImportProcessRequest request{{{"recipe", texture_image_decode_recipe},
                                  {"profile", texture_signed_image_decode_profile},
                                  {"format", unsigned(TextureFormat::BC5Snorm)},
                                  {"width", 4u},
                                  {"height", 4u}},
                                 {{"blocks.bin", endpoints}}};
    const auto output = execute_texture_image_recipe(request);
    check(output.size() == 1 && output[0].name == "rgba16le.bin" && output[0].bytes.size() == 128,
          "Signed worker output profile or extent differs");
    for (std::size_t i = 0; i < rg.rgba.size(); ++i)
        check(std::to_integer<unsigned>(output[0].bytes[i * 2]) == (rg.rgba[i] & 255) &&
                  std::to_integer<unsigned>(output[0].bytes[i * 2 + 1]) == (rg.rgba[i] >> 8),
              "Signed worker transport is not little endian");
    request.payload["profile"] = texture_image_decode_profile;
    reject([&] { execute_texture_image_recipe(request); }, "profile differs");
    request.payload["profile"] = texture_signed_image_decode_profile;
    request.payload["format"] = unsigned(TextureFormat::BC5);
    reject([&] { execute_texture_image_recipe(request); }, "profile differs");
    check(texture_image_worker_limits().file_bytes == 64ull * 1024 * 1024 &&
              texture_image_worker_limits(true).file_bytes == 128ull * 1024 * 1024 &&
              texture_image_worker_limits(true).memory_bytes ==
                  texture_image_worker_limits().memory_bytes,
          "Signed output changed unrelated worker limits");
}
void signed_process(const std::filesystem::path& worker, const std::filesystem::path& root) {
    for (const auto format : {TextureFormat::BC4Snorm, TextureFormat::BC5Snorm}) {
        const auto block = format == TextureFormat::BC4Snorm
                               ? scalar(129, 127)
                               : joined(scalar(129, 127), scalar(0, 0));
        const auto data = texture(format, 7, 5, block);
        const auto original = data.subresources;
        const auto expected = decode_texture_signed_block_image(format, 7, 5, data.subresources[0]);
        const auto actual = std::get<Rgba16Pixels>(texture_compressed_pixels(worker, root, data));
        check(actual.rgba == expected.rgba && actual.snorm_block_decoded &&
                  data.subresources == original,
              "Signed process changed source samples, reference values or provenance");
        const auto path = root / (std::to_string(unsigned(format)) + ".png");
        save_image_pixels(path, actual);
        const auto bytes = read_bytes(path, 1024 * 1024);
        ForgeImagePixels decoded{};
        const bool equal =
            forge_png_decode(bytes.data(), bytes.size(), 4096, 1024 * 1024, 0, &decoded) &&
            decoded.component_bytes == 2 && !decoded.srgb &&
            decoded.size == expected.rgba.size() * 2 &&
            std::memcmp(decoded.pixels, expected.rgba.data(), decoded.size) == 0;
        forge_image_pixels_free(&decoded);
        check(equal && !read_view_preview(path).png.empty(),
              "Signed PNG16 samples or inspection changed");
        for (const auto suffix : {".webp", ".jpg", ".hdr"}) {
            const auto invalid = root / (std::to_string(unsigned(format)) + suffix);
            reject([&] { save_image_pixels(invalid, actual); }, "PNG");
            check(!std::filesystem::exists(invalid), "Invalid signed output created a file");
        }
    }
    auto ambiguous = texture(TextureFormat::BC4Snorm, 4, 4, scalar(129, 128));
    reject([&] { texture_compressed_pixels(worker, root, ambiguous); }, "Ambiguous");
    auto cube = texture(TextureFormat::BC5Snorm, 8, 8, joined(scalar(129, 127), scalar(0, 0)));
    cube.dimension = TextureDimension::CubeArray;
    cube.layers = 2;
    cube.mips = 3;
    cube.subresources.clear();
    for (unsigned layer = 0; layer < 2; ++layer)
        for (unsigned face = 0; face < 6; ++face)
            for (unsigned mip = 0; mip < 3; ++mip) {
                const auto block = layer == 1 && face == 4 && mip == 2
                                       ? joined(scalar(0, 0), scalar(127, 129))
                                       : joined(scalar(129, 127), scalar(0, 0));
                auto plane = texture(TextureFormat::BC5Snorm, std::max(1u, 8u >> mip),
                                     std::max(1u, 8u >> mip), block);
                cube.subresources.push_back(std::move(plane.subresources[0]));
            }
    const auto selected =
        std::get<Rgba16Pixels>(texture_compressed_pixels(worker, root, cube, {2, 1, 4, 0}));
    check(selected.width == 2 && selected.height == 2, "Signed Cube mip extent changed");
    pixel(selected, 0, {32768, 65535, 0, 65535});
    reject([&] { texture_compressed_pixels(worker, root, cube, {3, 1, 4, 0}); }, "out of range");
    auto volume = texture(TextureFormat::BC4Snorm, 4, 4, scalar(129, 127));
    volume.dimension = TextureDimension::D3;
    volume.depth = 2;
    volume.subresources[0] = joined(scalar(129, 127), scalar(0, 0));
    const auto depth =
        std::get<Rgba16Pixels>(texture_compressed_pixels(worker, root, volume, {0, 0, 0, 1}));
    pixel(depth, 0, {32768, 0, 0, 65535});
    reject([&] { texture_compressed_pixels(worker, root, volume, {0, 0, 0, 2}); }, "out of range");
    // Exercise the actual 128 MiB output through the capped worker, not only a
    // small direct codec call that would also fit the older 64 MiB file limit.
    auto maximum = texture(TextureFormat::BC5Snorm, 4096, 4096, Block(16));
    const auto large = std::get<Rgba16Pixels>(texture_compressed_pixels(worker, root, maximum));
    check(large.rgba.size() * sizeof(std::uint16_t) == 128ull * 1024 * 1024 &&
              large.snorm_block_decoded,
          "Maximum signed output did not survive the real worker limits");
    pixel(large, 0, {32768, 32768, 0, 65535});
    pixel(large, 4096 * 4096 - 1, {32768, 32768, 0, 65535});
}
void process(const std::filesystem::path& worker, const std::filesystem::path& root) {
    std::filesystem::create_directories(root);
    auto data = texture(TextureFormat::BC1Srgb, 7, 5, color(0xf800, 0));
    // An opaque hint must not discard actual BC1 punch-through texels.
    data.alpha = TextureAlpha::Opaque;
    const auto expected = decode_texture_block_image(data.format, 7, 5, data.subresources[0]);
    const auto actual = unsigned_pixels(worker, root, data);
    check(actual.rgba == expected.rgba && actual.srgb && actual.block_decoded,
          "Supervised worker changed reference texels, color label or provenance");
    auto transparent = texture(TextureFormat::BC1Srgb, 4, 4, color(0x001f, 0xf800));
    transparent.alpha = TextureAlpha::Opaque;
    const auto punch = unsigned_pixels(worker, root, transparent);
    pixel(punch, 3, {0, 0, 0, 0});
    for (const char* suffix : {".png", ".webp"}) {
        const auto path = root / (std::string("decoded") + suffix);
        save_image_pixels(path, punch);
        check(!read_view_preview(path).png.empty(), "Decoded image inspection is missing");
        const auto old = read_bytes(path, 68 * 1024 * 1024);
        auto invalid = texture(TextureFormat::BC7Srgb, 4, 4, Block(16));
        reject([&] { save_image_pixels(path, unsigned_pixels(worker, root, invalid)); },
               "Invalid BC7 block");
        check(read_bytes(path, 68 * 1024 * 1024) == old,
              "Failed compressed decode replaced a good image");
    }
    save_image_pixels(root / "opaque.jpg", actual);
    reject([&] { save_image_pixels(root / "opaque.jpg", punch); }, "opaque");
    auto cube = texture(TextureFormat::BC1, 8, 8, color(0xf800, 0, 0));
    cube.dimension = TextureDimension::CubeArray;
    cube.layers = 2;
    cube.mips = 3;
    cube.subresources.clear();
    for (unsigned layer = 0; layer < 2; ++layer)
        for (unsigned face = 0; face < 6; ++face)
            for (unsigned mip = 0; mip < 3; ++mip) {
                auto plane =
                    texture(TextureFormat::BC1, std::max(1u, 8u >> mip), std::max(1u, 8u >> mip),
                            color(layer == 1 && face == 4 && mip == 2 ? 0x07e0 : 0xf800, 0, 0));
                cube.subresources.push_back(std::move(plane.subresources[0]));
            }
    const auto selected = unsigned_pixels(worker, root, cube, {2, 1, 4, 0});
    check(selected.width == 2 && selected.height == 2, "Cube mip selector changed extent");
    pixel(selected, 0, {0, 255, 0, 255});
    reject([&] { unsigned_pixels(worker, root, cube, {3, 0, 0, 0}); }, "out of range");
    reject([&] { unsigned_pixels(worker, root, cube, {0, 2, 0, 0}); }, "out of range");
    reject([&] { unsigned_pixels(worker, root, cube, {0, 0, 6, 0}); }, "out of range");
    auto volume = texture(TextureFormat::BC4, 4, 4, scalar(255, 0));
    volume.dimension = TextureDimension::D3;
    volume.depth = 2;
    volume.subresources[0] = joined(scalar(255, 0), scalar(10, 110));
    const auto depth = unsigned_pixels(worker, root, volume, {0, 0, 0, 1});
    pixel(depth, 0, {10, 0, 0, 255});
    reject([&] { unsigned_pixels(worker, root, volume, {0, 0, 0, 2}); }, "out of range");
    TextureData oversized;
    oversized.width = oversized.height = 16384;
    oversized.format = TextureFormat::BC7;
    reject([&] { unsigned_pixels(worker, root, oversized); }, "payload exceeds budget");
    const auto wide = texture(TextureFormat::BC1, 8192, 4, color(0xf800, 0));
    reject([&] { unsigned_pixels(worker, root, wide); }, "4096 pixels per axis");
    std::stop_source stopped;
    stopped.request_stop();
    reject([&] { unsigned_pixels(worker, root, data, {}, stopped.get_token()); }, "cancelled");
    reject([&] { unsigned_pixels({}, root, data); }, "packaged asset worker");
    for (const auto& entry : std::filesystem::directory_iterator(root / ".forge/jobs"))
        check(!entry.is_directory(), "Finished image decode retained a staging payload");
}
} // namespace
int main(int argc, char** argv) {
    try {
        numeric();
        protocol();
        signed_numeric();
        if (argc == 3)
            process(std::filesystem::u8path(argv[1]), std::filesystem::u8path(argv[2]));
        if (argc == 3)
            signed_process(std::filesystem::u8path(argv[1]), std::filesystem::u8path(argv[2]));
        std::cout << "Compressed Texture reference vectors, admission and worker path passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
