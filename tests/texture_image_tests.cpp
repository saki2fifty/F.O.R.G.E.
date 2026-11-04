#include "asset_bytes.hpp"
#include "texture_decode.h"
#include "texture_image.hpp"
#include "view_image.hpp"
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace forge;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void reject(F action, std::string_view reason = {}) {
    try {
        action();
    } catch (const std::exception& error) {
        check(reason.empty() ||
                  std::string_view(error.what()).find(reason) != std::string_view::npos,
              "Texture image rejected for an unexpected reason");
        return;
    }
    throw std::runtime_error("Invalid Texture image projection admitted");
}
std::vector<std::byte> bytes(std::initializer_list<unsigned> values) {
    std::vector<std::byte> result;
    for (auto value : values)
        result.push_back(std::byte(value));
    return result;
}
} // namespace
int main() {
    try {
        TextureData rgba;
        rgba.width = rgba.height = 2;
        rgba.format = TextureFormat::RGBA8Srgb;
        rgba.alpha = TextureAlpha::Straight;
        rgba.subresources = {bytes({255, 1, 2, 0, 3, 4, 5, 127, 6, 7, 8, 255, 9, 10, 11, 64})};
        const std::vector<std::uint8_t> expected{255, 1, 2, 0,   3, 4,  5,  127,
                                                 6,   7, 8, 255, 9, 10, 11, 64};
        const auto image = texture_stored_pixels(rgba);
        check(image.width == 2 && image.height == 2 && image.rgba == expected && image.srgb,
              "Stored sRGB pixels, alpha, hidden RGB or top-row orientation changed");
        rgba.format = TextureFormat::RGBA8;
        rgba.semantic = TextureSemantic::Normal;
        rgba.alpha = TextureAlpha::Premultiplied;
        check(texture_stored_pixels(rgba).rgba == expected,
              "Stored pixel export applied color, normal or alpha interpretation");
        const auto directory = std::filesystem::temp_directory_path() /
                               ("forge-texture-image-" + AssetId::generate().str());
        check(std::filesystem::create_directory(directory), "Cannot create unique image fixture");
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::error_code error;
                std::filesystem::remove_all(path, error);
            }
        } cleanup{directory};
        struct Decoded : ForgeImagePixels {
            Decoded() : ForgeImagePixels{} {}
            ~Decoded() { forge_image_pixels_free(this); }
        };
        for (const char* suffix : {".png", ".webp"}) {
            const auto path = directory / (std::string("pixels") + suffix);
            save_image_pixels(path, image);
            const auto file = asset_detail::read_bytes(path, 68 * 1024 * 1024);
            Decoded decoded;
            const auto decode =
                std::string_view(suffix) == ".png" ? forge_png_decode : forge_webp_decode;
            check(decode(file.data(), file.size(), 4096, 64 * 1024 * 1024, 0, &decoded) &&
                      decoded.width == 2 && decoded.height == 2 && decoded.channels == 4 &&
                      decoded.component_bytes == 1 && decoded.size == expected.size() &&
                      decoded.srgb &&
                      std::memcmp(decoded.pixels, expected.data(), expected.size()) == 0,
                  "PNG/WebP output changed stored samples, alpha or orientation");
        }
        const auto unlabelled = texture_stored_pixels(rgba);
        check(!unlabelled.srgb, "Linear/data channels gained an sRGB label");
        const auto data_path = directory / "data.png";
        save_image_pixels(data_path, unlabelled);
        const auto encoded = asset_detail::read_bytes(data_path, 68 * 1024 * 1024);
        Decoded decoded;
        check(
            forge_png_decode(encoded.data(), encoded.size(), 4096, 64 * 1024 * 1024, 0, &decoded) &&
                !decoded.srgb && decoded.size == expected.size() &&
                std::memcmp(decoded.pixels, expected.data(), expected.size()) == 0,
            "Data PNG invented sRGB intent or converted its stored samples");
        const auto preview = read_view_preview(data_path);
        Decoded preview_data;
        check(forge_png_decode(preview.png.data(), preview.png.size(), 4096, 64 * 1024 * 1024, 0,
                               &preview_data) &&
                  !preview_data.srgb && preview_data.size == expected.size() &&
                  std::memcmp(preview_data.pixels, expected.data(), expected.size()) == 0,
              "Reading a stored-data image preview changed its label or samples");
        const auto old = asset_detail::read_bytes(directory / "pixels.webp", 68 * 1024 * 1024);
        reject([&] { save_image_pixels(directory / "pixels.webp", unlabelled); }, "require PNG");
        check(asset_detail::read_bytes(directory / "pixels.webp", 68 * 1024 * 1024) == old,
              "Rejected WebP storage replaced a good file");
        TextureData red;
        red.width = 2;
        red.height = 1;
        red.format = TextureFormat::R8;
        red.semantic = TextureSemantic::Data;
        red.alpha = TextureAlpha::Opaque;
        red.subresources = {bytes({17, 23})};
        check(texture_stored_pixels(red).rgba ==
                  std::vector<std::uint8_t>{17, 0, 0, 255, 23, 0, 0, 255},
              "R8 projection lost stored values or default channels");
        red.format = TextureFormat::RG8;
        red.subresources = {bytes({1, 2, 3, 4})};
        check(texture_stored_pixels(red).rgba ==
                  std::vector<std::uint8_t>{1, 2, 0, 255, 3, 4, 0, 255},
              "RG8 projection interpreted the second channel as alpha");
        TextureData cubes;
        cubes.dimension = TextureDimension::CubeArray;
        cubes.width = cubes.height = 2;
        cubes.layers = 2;
        cubes.mips = 2;
        for (unsigned i = 0; i < 24; ++i)
            cubes.subresources.emplace_back(i % 2 ? 4 : 16, std::byte{50});
        cubes.subresources.back() = bytes({9, 8, 7, 6});
        check(texture_stored_pixels(cubes, {1, 1, 5, 0}).rgba ==
                  std::vector<std::uint8_t>{9, 8, 7, 6},
              "Cube-array mip/layer/face selection read another subresource");
        reject([&] { texture_stored_pixels(cubes, {0, 2, 0, 0}); });
        reject([&] { texture_stored_pixels(cubes, {0, 0, 6, 0}); });
        reject([&] { texture_stored_pixels(cubes, {2, 0, 0, 0}); });
        cubes.subresources.front().pop_back();
        reject([&] { texture_stored_pixels(cubes, {1, 1, 5, 0}); });

        TextureData volume;
        volume.dimension = TextureDimension::D3;
        volume.width = volume.depth = 2;
        volume.height = 1;
        volume.mips = 2;
        volume.subresources = {bytes({1, 0, 0, 255, 2, 0, 0, 255, 3, 0, 0, 255, 4, 0, 0, 255}),
                               bytes({9, 8, 7, 6})};
        check(texture_stored_pixels(volume, {0, 0, 0, 1}).rgba ==
                  std::vector<std::uint8_t>{3, 0, 0, 255, 4, 0, 0, 255},
              "Volume image projection read another depth plane");
        check(texture_stored_pixels(volume, {1, 0, 0, 0}).rgba ==
                  std::vector<std::uint8_t>{9, 8, 7, 6},
              "Volume mip layout did not shrink its depth");
        reject([&] { texture_stored_pixels(volume, {1, 0, 0, 1}); });
        reject([&] { texture_stored_pixels(volume, {0, 0, 1, 0}); });

        TextureData unsupported;
        unsupported.width = unsupported.height = 4;
        unsupported.format = TextureFormat::BC1;
        unsupported.subresources = {std::vector<std::byte>(8)};
        reject([&] { texture_stored_pixels(unsupported); }, "requires uncompressed");
        unsupported.format = TextureFormat::RGBA16Float;
        unsupported.semantic = TextureSemantic::HdrColor;
        unsupported.subresources = {std::vector<std::byte>(4 * 4 * 8)};
        reject([&] { texture_stored_pixels(unsupported); }, "requires uncompressed");
        TextureData wide;
        wide.width = 4097;
        wide.height = 1;
        wide.subresources = {std::vector<std::byte>(4097 * 4)};
        reject([&] { texture_stored_pixels(wide); }, "4096 pixels");
        wide.width = 16384;
        wide.height = 4096;
        wide.subresources.clear();
        reject([&] { texture_stored_pixels(wide); }, "budget");
        std::stop_source cancellation;
        cancellation.request_stop();
        reject([&] { texture_stored_pixels(rgba, {}, cancellation.get_token()); }, "cancelled");
        std::cout << "Stored Texture channels, alpha, orientation, slice selection, admission, "
                     "limits and cancellation passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
