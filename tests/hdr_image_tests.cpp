#include "asset_bytes.hpp"
#include "texture_decode.h"
#include "texture_image.hpp"
#include "texture_import.hpp"
#include "view_image.hpp"
#include "view_image_hdr.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace forge;
namespace {
void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F> void reject(F action, std::string_view reason = {}) {
    try {
        action();
    } catch (const std::exception& error) {
        check(reason.empty() ||
                  std::string_view(error.what()).find(reason) != std::string_view::npos,
              "HDR rejected for an unexpected reason");
        return;
    }
    throw std::runtime_error("Invalid HDR image admitted");
}
void append(std::vector<std::byte>& output, std::uint32_t value, unsigned count) {
    for (unsigned i = 0; i < count; ++i)
        output.push_back(std::byte((value >> (8 * i)) & 255));
}
std::vector<std::byte> packed(std::initializer_list<float> values) {
    std::vector<std::byte> result;
    for (float value : values)
        append(result, std::bit_cast<std::uint32_t>(value), 4);
    return result;
}
std::vector<std::byte> halves(std::initializer_list<unsigned> values) {
    std::vector<std::byte> result;
    for (auto value : values)
        append(result, value, 2);
    return result;
}
float read_float(const std::byte* at) {
    const std::array<std::byte, 4> bits{at[0], at[1], at[2], at[3]};
    if constexpr (std::endian::native == std::endian::little)
        return std::bit_cast<float>(bits);
    else
        return std::bit_cast<float>(std::array<std::byte, 4>{at[3], at[2], at[1], at[0]});
}
std::vector<std::uint8_t> preview_bytes(const ViewImagePreview& preview) {
    struct Decoded : ForgeImagePixels {
        Decoded() : ForgeImagePixels{} {}
        ~Decoded() { forge_image_pixels_free(this); }
    } decoded;
    check(forge_png_decode(preview.png.data(), preview.png.size(), 4096,
                           FORGE_VIEW_IMAGE_PIXEL_LIMIT, 0, &decoded) &&
              decoded.component_bytes == 1 && decoded.channels == 4 && decoded.srgb &&
              decoded.width == preview.width && decoded.height == preview.height,
          "HDR inspection is not bounded opaque sRGB PNG8");
    return {decoded.pixels, decoded.pixels + decoded.size};
}
std::size_t payload_start(const std::vector<std::byte>& file) {
    const std::string text(reinterpret_cast<const char*>(file.data()), file.size());
    const auto resolution = text.find("\n-Y ");
    check(resolution != std::string::npos, "Fixture resolution is missing");
    return text.find('\n', resolution + 1) + 1;
}
void inspection_guards(const std::vector<std::byte>& flat) {
    const auto payload = payload_start(flat);
    auto invalid = flat;
    invalid.push_back(std::byte{0});
    reject([&] { preview_hdr_image(invalid); }, "Trailing");
    invalid = flat;
    invalid.pop_back();
    reject([&] { preview_hdr_image(invalid); }, "Truncated");
    invalid = flat;
    invalid[10] = std::byte{'X'};
    reject([&] { preview_hdr_image(invalid); }, "fixed working-primary");
    invalid = flat;
    invalid[payload + 3] = std::byte{0};
    reject([&] { preview_hdr_image(invalid); }, "normalized RGBE");
    auto resolution = [&](const std::string& replacement) {
        std::string text(reinterpret_cast<const char*>(flat.data()), flat.size());
        const auto start = text.find("-Y 1 +X 3\n");
        text.replace(start, 10, replacement);
        return std::vector<std::byte>(
            reinterpret_cast<const std::byte*>(text.data()),
            reinterpret_cast<const std::byte*>(text.data() + text.size()));
    };
    for (const auto& text :
         {"-Y 0 +X 3\n", "-Y 1 +X 4097\n", "-Y -1 +X 3\n", "-Y 1 +X 3 trailing\n"}) {
        const auto file = resolution(text);
        reject([&] { preview_hdr_image(file); });
    }
    const auto absent_large_payload = resolution("-Y 4096 +X 4096\n");
    reject([&] { preview_hdr_image(absent_large_payload); }, "Truncated");
    const auto packet = encode_hdr_image({8, 1, std::vector<float>(24, 1)});
    const auto at = payload_start(packet);
    for (unsigned value : {0u, 9u, 128u, 255u}) {
        invalid = packet;
        invalid[at + 4] = std::byte(value);
        reject([&] { preview_hdr_image(invalid); }, "literal packet");
    }
    invalid = packet;
    invalid[at + 3] = std::byte{7};
    reject([&] { preview_hdr_image(invalid); }, "scanline header");
    for (std::size_t length = at; length < packet.size(); ++length)
        reject([&] { preview_hdr_image(std::span(packet).first(length)); }, "Truncated");
    std::stop_source stopped;
    stopped.request_stop();
    reject([&] { preview_hdr_image(flat, stopped.get_token()); }, "cancelled");
    const std::vector<std::byte> oversized(FORGE_VIEW_IMAGE_ENCODED_LIMIT + 1);
    reject([&] { preview_hdr_image(oversized); }, "encoded byte budget");
}
} // namespace
int main() {
    try {
        Rgb32Pixels known{3, 1, {1, .5f, .25f, 4, .25f, .125f, 0, 0, 0}};
        const auto encoded = encode_hdr_image(known);
        const auto directory = std::filesystem::temp_directory_path() /
                               ("forge-hdr-image-" + AssetId::generate().str());
        check(std::filesystem::create_directory(directory), "Cannot create HDR output fixture");
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::error_code error;
                std::filesystem::remove_all(path, error);
            }
        } cleanup{directory};
        const auto destination = directory / "radiance.HDR";
        save_image_pixels(destination, ImagePixels{known});
        check(asset_detail::read_bytes(destination, FORGE_VIEW_IMAGE_ENCODED_LIMIT) == encoded &&
                  view_image_format(destination) == ViewImageFormat::Hdr &&
                  std::string_view(view_image_mime(ViewImageFormat::Hdr)) == "image/vnd.radiance",
              "Shared image writer changed HDR bytes, suffix or MIME");
        reject([&] { save_image_pixels(destination, Rgba8Pixels{1, 1, {1, 2, 3, 255}}); },
               "linear HDR");
        reject([&] { save_image_pixels(destination, Rgb32Pixels{1, 1, {-1, 0, 0}}); });
        check(asset_detail::read_bytes(destination, FORGE_VIEW_IMAGE_ENCODED_LIMIT) == encoded,
              "Rejected HDR conversion or samples replaced an existing good image");
        reject([&] { save_image_pixels(directory / "wrong.png", known); }, ".hdr");
        const auto file_preview = read_view_preview(destination);
        const auto typed_preview = preview_image_pixels(ImagePixels{known});
        check(file_preview.png == typed_preview.png && file_preview.width == 3 &&
                  file_preview.height == 1 && file_preview.profile == "hdr_reinhard_srgb" &&
                  typed_preview.profile == file_preview.profile,
              "Owned/typed HDR inspection disagrees or loses its interpretation label");
        check(preview_bytes(file_preview) ==
                  std::vector<std::uint8_t>{188, 156, 124, 255, 231, 124, 94, 255, 0, 0, 0, 255},
              "HDR inspection differs from independent unit-exposure/sRGB numeric vectors");
        const auto quantized =
            preview_image_pixels(ImagePixels{Rgb32Pixels{1, 1, {1, .003f, .0001f}}});
        check(preview_bytes(quantized) == std::vector<std::uint8_t>{188, 0, 0, 255},
              "HDR inspection substituted unquantized source samples for the written artifact");
        inspection_guards(encoded);
        const auto no_suffix = directory / "owned-bytes";
        save_view_png(no_suffix, 1, 1, std::array<std::uint8_t, 4>{255, 0, 0, 255});
        check(read_view_preview(no_suffix).width == 1,
              "HDR signature admission broke legacy header-based PNG preview reads");
        const std::array<std::byte, 12> expected{std::byte{128}, std::byte{64},  std::byte{32},
                                                 std::byte{129}, std::byte{128}, std::byte{8},
                                                 std::byte{4},   std::byte{131}, std::byte{0},
                                                 std::byte{0},   std::byte{0},   std::byte{0}};
        check(std::equal(expected.begin(), expected.end(), encoded.end() - expected.size()),
              "Independent known RGBE numeric vector differs");
        const std::string text(reinterpret_cast<const char*>(encoded.data()), encoded.size());
        check(text.starts_with("#?RADIANCE\nSOFTWARE=FORGE\nFORMAT=32-bit_rle_rgbe\n") &&
                  text.find("PRIMARIES=0.6400 0.3300 0.3000 0.6000 0.1500 0.0600 0.3127 0.3290") !=
                      std::string::npos &&
                  text.find("\n-Y 1 +X 3\n") != std::string::npos,
              "HDR header lost the working primaries or top-row orientation");
        for (unsigned width : {1u, 7u, 8u, 126u, 127u, 128u, 129u, 254u, 255u, 256u, 4096u}) {
            Rgb32Pixels pixels{width, 2, std::vector<float>(std::size_t(width) * 2 * 3)};
            for (unsigned y = 0; y < 2; ++y)
                for (unsigned x = 0; x < width; ++x) {
                    const auto at = (std::size_t(y) * width + x) * 3;
                    pixels.rgb[at] = .125f * (x % 31) + y * 8;
                    pixels.rgb[at + 1] = .0625f + y * 4;
                    pixels.rgb[at + 2] = .001f * x;
                }
            const auto file = encode_hdr_image(pixels);
            const auto preview = preview_hdr_image(file);
            check(preview.source_width == width && preview.source_height == 2 &&
                      preview.width <= 1024 && preview.height <= 1024 &&
                      preview.profile == "hdr_reinhard_srgb" && !preview.png.empty(),
                  "HDR flat/packet inspection lost bounded extent or interpretation");
            asset_detail::validate_hdr_extent(file, 4096);
            asset_detail::TextureImportSettings settings;
            settings.srgb = false;
            settings.generate_mips = false;
            settings.semantic = TextureSemantic::HdrColor;
            const auto decoded = asset_detail::import_texture_image(file, "owned.hdr", settings);
            check(decoded.width == width && decoded.height == 2 && decoded.mips == 1 &&
                      decoded.format == TextureFormat::RGBA32Float &&
                      decoded.semantic == TextureSemantic::HdrColor &&
                      decoded.alpha == TextureAlpha::Opaque,
                  "Existing HDR decoder did not accept emitted flat/channel packets");
            const auto read = texture_hdr_pixels(decoded);
            for (std::size_t at = 0; at < pixels.rgb.size(); at += 3) {
                const float peak =
                    std::max({pixels.rgb[at], pixels.rgb[at + 1], pixels.rgb[at + 2]});
                int exponent;
                std::frexp(peak, &exponent);
                const float quantum = std::ldexp(1.f, exponent - 8);
                for (unsigned c = 0; c < 3; ++c)
                    check(read.rgb[at + c] <= pixels.rgb[at + c] &&
                              pixels.rgb[at + c] - read.rgb[at + c] <= quantum,
                          "HDR decoding changed orientation, range or exceeded quantization");
            }
            check(read_float(decoded.subresources[0].data() + 12) == 1,
                  "HDR decoding did not produce opaque alpha");
        }
        const Rgb32Pixels range{2, 1, {std::ldexp(1.f, -128), 0, 0, std::ldexp(1.f, 126), 0, 0}};
        const auto limits = encode_hdr_image(range);
        check(limits[limits.size() - 5] == std::byte{1} && limits.back() == std::byte{255},
              "HDR valid exponent boundaries changed");
        check(preview_bytes(preview_hdr_image(limits)) ==
                  std::vector<std::uint8_t>{0, 0, 0, 255, 255, 0, 0, 255},
              "HDR inspection changed extreme exponent handling");
        for (float invalid :
             {-1.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN(),
              std::ldexp(1.f, -129), std::ldexp(1.f, 127)})
            reject([&] { encode_hdr_image({1, 1, {invalid, 0, 0}}); });
        reject([&] { encode_hdr_image({0, 1, {}}); });
        reject([&] { encode_hdr_image({4097, 1, {}}); });
        reject([&] { encode_hdr_image({1, 1, {1, 1}}); });
        reject([&] { encode_hdr_image(known, 16); }, "byte budget");
        reject([&] { encode_hdr_image(known, FORGE_VIEW_IMAGE_ENCODED_LIMIT + 1); });
        std::stop_source stop;
        stop.request_stop();
        reject([&] { encode_hdr_image(known, FORGE_VIEW_IMAGE_ENCODED_LIMIT, stop.get_token()); },
               "cancelled");
        TextureData source;
        source.width = source.height = 1;
        source.format = TextureFormat::RGBA32Float;
        source.semantic = TextureSemantic::HdrColor;
        source.subresources = {packed({8, .125f, .0625f, 1})};
        check(texture_hdr_pixels(source).rgb == std::vector<float>{8, .125f, .0625f},
              "HDR projection applied exposure or quantized float storage");
        source.subresources = {packed({8, 0, 0, .5f})};
        reject([&] { texture_hdr_pixels(source); }, "alpha");
        source.subresources = {packed({-1, 0, 0, 1})};
        reject([&] { texture_hdr_pixels(source); }, "nonnegative");
        source.subresources = {packed({std::numeric_limits<float>::infinity(), 0, 0, 1})};
        reject([&] { texture_hdr_pixels(source); }, "finite");
        source.format = TextureFormat::RGBA16Float;
        source.subresources = {halves({0x4000, 0x3800, 1, 0x3c00})};
        check(texture_hdr_pixels(source).rgb == std::vector<float>{2, .5f, std::ldexp(1.f, -24)},
              "Half-float normal/subnormal conversion changed samples");
        source.width = source.height = 2;
        source.subresources = {
            halves({0x3c00, 0x4000, 0x4200, 0x3c00, 0x4400, 0x4500, 0x4600, 0x3c00, 0x4700, 0x4800,
                    0x4880, 0x3c00, 0x4900, 0x4980, 0x4a00, 0x3c00})};
        check(texture_hdr_pixels(source).rgb ==
                  std::vector<float>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12},
              "Half-float rows or channel strides changed the stored image");
        source.width = source.height = 1;
        source.subresources = {halves({0x7c00, 0, 0, 0x3c00})};
        reject([&] { texture_hdr_pixels(source); }, "finite");
        source.subresources = {halves({0x7e00, 0, 0, 0x3c00})};
        reject([&] { texture_hdr_pixels(source); }, "finite");
        source.subresources = {halves({0x8000, 0, 0, 0x3c00})};
        check(texture_hdr_pixels(source).rgb[0] == 0, "Signed zero was rejected");
        source.semantic = TextureSemantic::Color;
        reject([&] { texture_hdr_pixels(source); }, "HdrColor");
        source.semantic = TextureSemantic::HdrColor;
        source.dimension = TextureDimension::CubeArray;
        source.width = source.height = 2;
        source.layers = source.mips = 2;
        source.subresources.clear();
        for (unsigned i = 0; i < 24; ++i) {
            auto& bytes = source.subresources.emplace_back();
            for (unsigned pixel = 0; pixel < (i % 2 ? 1u : 4u); ++pixel) {
                const auto rgba = halves({0x3c00, 0, 0, 0x3c00});
                bytes.insert(bytes.end(), rgba.begin(), rgba.end());
            }
        }
        source.subresources.back() = halves({0x4400, 0x3800, 0x4000, 0x3c00});
        check(texture_hdr_pixels(source, {1, 1, 5, 0}).rgb == std::vector<float>{4, .5f, 2},
              "HDR cube-array projection selected another mip/layer/face");
        reject([&] { texture_hdr_pixels(source, {1, 2, 5, 0}); });
        reject([&] { texture_hdr_pixels(source, {1, 1, 6, 0}); });
        reject([&] { texture_hdr_pixels(source, {1, 1, 5, 1}); });
        reject([&] { texture_hdr_pixels(source, {}, stop.get_token()); }, "cancelled");
        TextureData too_large;
        too_large.format = TextureFormat::RGBA32Float;
        too_large.semantic = TextureSemantic::HdrColor;
        too_large.width = too_large.height = 4096;
        too_large.mips = 2;
        reject([&] { validate_texture_metadata(too_large, {texture_hdr_source_limit}); });
        reject([&] { texture_hdr_pixels(too_large); });
        source.subresources[0].pop_back();
        reject([&] { texture_hdr_pixels(source, {1, 1, 5, 0}); });
        std::cout << "HDR projection, opaque/range guards, independent RGBE vectors and "
                     "pinned-decoder flat/packet conformance passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
