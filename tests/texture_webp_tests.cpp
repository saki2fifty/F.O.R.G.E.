#include "texture_import.hpp"
#include <cstring>
#include <iostream>
#include <source_location>
#include <webp/encode.h>
using namespace forge;
using namespace forge::asset_detail;
namespace {
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F> void rejects(F f, std::source_location where = std::source_location::current()) {
    try {
        f();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid WebP accepted at line " + std::to_string(where.line()));
}
void put(std::vector<std::byte>& b, unsigned at, unsigned value) {
    for (unsigned i = 0; i < 4; ++i)
        b.at(at + i) = std::byte((value >> (i * 8)) & 255);
}
std::vector<std::byte> fixture(bool lossless) {
    const std::array<std::uint8_t, 16> pixels{255, 0, 0, 255, 255, 0, 0, 128,
                                              255, 0, 0, 255, 255, 0, 0, 255};
    std::uint8_t* data = nullptr;
    const auto size = lossless ? WebPEncodeLosslessRGBA(pixels.data(), 2, 2, 8, &data)
                               : WebPEncodeRGBA(pixels.data(), 2, 2, 8, 90, &data);
    require(size && data, "Native WebP fixture encode failed");
    const auto* begin = reinterpret_cast<const std::byte*>(data);
    std::vector<std::byte> result(begin, begin + size);
    WebPFree(data);
    return result;
}
} // namespace
int main() {
    try {
        TextureImportSettings settings;
        for (bool lossless : {false, true}) {
            const auto bytes = fixture(lossless);
            const auto texture = import_texture_image(bytes, "test.webp", settings);
            require(texture.width == 2 && texture.height == 2 && texture.mips == 2 &&
                        texture.format == TextureFormat::RGBA8Srgb &&
                        texture.alpha == TextureAlpha::Straight &&
                        std::to_integer<unsigned>(texture.subresources[0][0]) >= 240 &&
                        texture.subresources[0][7] == std::byte{128},
                    "WebP color, alpha or mips changed");
            for (std::size_t n = 0; n < bytes.size(); ++n) {
                const std::vector<std::byte> truncated(bytes.begin(), bytes.begin() + n);
                rejects([&] { import_texture_image(truncated, "bad.webp", settings); });
            }
            auto corrupt = bytes;
            corrupt.push_back(std::byte{});
            rejects([&] { import_texture_image(corrupt, "bad.webp", settings); });
            corrupt = bytes;
            put(corrupt, 16, 0xffffffffu);
            rejects([&] { import_texture_image(corrupt, "bad.webp", settings); });
        }
        auto bytes = fixture(true);
        bytes.insert(bytes.end(), 12, std::byte{});
        std::memcpy(bytes.data() + bytes.size() - 12, "ICCP", 4);
        put(bytes, unsigned(bytes.size() - 8), 4);
        put(bytes, 4, unsigned(bytes.size() - 8));
        rejects([&] { import_texture_image(bytes, "profile.webp", settings); });
        // Valid native lossy RGBA fixture contains VP8X; animation is not a still texture.
        bytes = fixture(false);
        require(std::memcmp(bytes.data() + 12, "VP8X", 4) == 0, "Expected extended WebP fixture");
        bytes[20] |= std::byte{2};
        rejects([&] { import_texture_image(bytes, "animation.webp", settings); });
        bytes = fixture(true);
        TextureLimits limits;
        limits.dimension = 1;
        rejects([&] { import_texture_image(bytes, "large.webp", settings, limits); });
        limits = {};
        limits.bytes = 12;
        rejects([&] { import_texture_image(bytes, "large.webp", settings, limits); });
        settings.srgb = false;
        settings.semantic = TextureSemantic::Data;
        require(import_texture_image(bytes, "data.webp", settings).format == TextureFormat::RGBA8,
                "WebP data channels forced sRGB");
        std::stop_source stop;
        stop.request_stop();
        rejects(
            [&] { import_texture_image(bytes, "cancel.webp", settings, {}, stop.get_token()); });
        std::cout << "Native lossy/lossless WebP preparation and safe rejection passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
