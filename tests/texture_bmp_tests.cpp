#include "texture_import.hpp"
#include <iostream>
#include <source_location>
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
    throw std::runtime_error("Invalid BMP accepted at line " + std::to_string(where.line()));
}
void put(std::vector<std::byte>& b, unsigned at, unsigned value, unsigned count = 4) {
    for (unsigned i = 0; i < count; ++i)
        b.at(at + i) = std::byte((value >> (8 * i)) & 255);
}
std::vector<std::byte> fixture(unsigned bits, unsigned header = 40, bool top = false,
                               bool fields = false) {
    const unsigned palette = bits < 16 ? 2 : 0;
    const unsigned offset = 14 + header + (fields && header == 40 ? 12 : 0) + palette * 4;
    const unsigned row = ((3 * bits + 31) / 32) * 4;
    std::vector<std::byte> b(offset + row * 2);
    put(b, 0, 0x4d42, 2);
    put(b, 2, unsigned(b.size()));
    put(b, 10, offset);
    put(b, 14, header);
    put(b, 18, 3);
    put(b, 22, top ? unsigned(-2) : 2);
    put(b, 26, 1, 2);
    put(b, 28, bits, 2);
    put(b, 30, fields ? 3 : 0);
    put(b, 46, palette);
    if (fields) {
        put(b, 54, bits == 16 ? 0xf800 : 0xff0000);
        put(b, 58, bits == 16 ? 0x07e0 : 0xff00);
        put(b, 62, bits == 16 ? 0x001f : 0xff);
        if (header >= 108)
            put(b, 66, bits == 32 ? 0xff000000 : 0);
    }
    if (header >= 108)
        put(b, 70, 0x73524742);
    if (palette) {
        b[14 + header + 4 + 2] = std::byte{255}; // red entry1
        for (unsigned y = 0; y < 2; ++y) {
            if (bits == 1)
                b[offset + y * row] = std::byte{0xe0};
            if (bits == 4) {
                b[offset + y * row] = std::byte{0x11};
                b[offset + y * row + 1] = std::byte{0x10};
            }
            if (bits == 8)
                for (unsigned x = 0; x < 3; ++x)
                    b[offset + y * row + x] = std::byte{1};
        }
    } else {
        // Top row red, bottom row blue regardless of stored orientation.
        for (unsigned y = 0; y < 2; ++y)
            for (unsigned x = 0; x < 3; ++x) {
                const bool red = (top ? y == 0 : y == 1);
                const unsigned at = offset + y * row + x * bits / 8;
                if (bits == 16)
                    put(b, at, red ? (fields ? 0xf800 : 0x7c00) : 0x1f, 2);
                else {
                    b[at + (red ? 2 : 0)] = std::byte{255};
                    if (bits == 32)
                        b[at + 3] = std::byte{255};
                }
            }
    }
    return b;
}
} // namespace
int main() {
    try {
        TextureImportSettings settings;
        settings.generate_mips = false;
        for (auto bits : {1u, 4u, 8u, 16u, 24u, 32u})
            for (bool top : {false, true}) {
                const auto b = fixture(bits, 40, top);
                const auto t = import_texture_image(b, "fixture.bmp", settings);
                require(t.width == 3 && t.height == 2 && t.format == TextureFormat::RGBA8Srgb &&
                            t.subresources[0][0] == std::byte{255} &&
                            t.subresources[0][2] == std::byte{0},
                        "BMP palette/channel/top-row conversion failed");
                for (std::size_t n = 0; n < b.size(); ++n) {
                    const std::vector<std::byte> short_file(b.begin(), b.begin() + n);
                    rejects([&] { import_texture_image(short_file, "bad.bmp", settings); });
                }
            }
        for (auto header : {40u, 108u, 124u})
            for (auto bits : {16u, 32u}) {
                auto b = fixture(bits, header, true, true);
                auto t = import_texture_image(b, "fields.bmp", settings);
                require(t.subresources[0][0] == std::byte{255} &&
                            t.subresources[0][2] == std::byte{0},
                        "BMP explicit masks changed color");
                put(b, 58, 0x15); // sparse/overlapping mask
                rejects([&] { import_texture_image(b, "bad.bmp", settings); });
            }
        auto alpha = fixture(32, 124, true, true);
        for (unsigned at = 14 + 124 + 3; at < alpha.size(); at += 4)
            alpha[at] = std::byte{0};
        require(import_texture_image(alpha, "alpha.bmp", settings).alpha == TextureAlpha::Straight,
                "Explicit zero bitfield alpha was made opaque");
        auto legacy = fixture(32);
        for (unsigned at = 54 + 3; at < legacy.size(); at += 4)
            legacy[at] = std::byte{0};
        require(import_texture_image(legacy, "legacy.bmp", settings).alpha == TextureAlpha::Opaque,
                "Legacy unused alpha did not follow native BMP policy");
        auto b = fixture(24);
        put(b, 22, 0x80000000u);
        rejects([&] { import_texture_image(b, "bad.bmp", settings); });
        b = fixture(24);
        put(b, 10, 0);
        rejects([&] { import_texture_image(b, "bad.bmp", settings); });
        b = fixture(8);
        b[62] = std::byte{2};
        rejects([&] { import_texture_image(b, "bad.bmp", settings); });
        b = fixture(24);
        put(b, 30, 1);
        rejects([&] { import_texture_image(b, "bad.bmp", settings); });
        b = fixture(24, 124);
        put(b, 70, 0);
        rejects([&] { import_texture_image(b, "profile.bmp", settings); });
        b = fixture(24, 124);
        put(b, 126, 138);
        rejects([&] { import_texture_image(b, "profile.bmp", settings); });
        b = fixture(24);
        TextureLimits limits;
        limits.dimension = 2;
        rejects([&] { import_texture_image(b, "large.bmp", settings, limits); });
        limits = {};
        limits.bytes = 12;
        rejects([&] { import_texture_image(b, "large.bmp", settings, limits); });
        settings.generate_mips = true;
        require(import_texture_image(b, "mips.bmp", settings).mips == 2,
                "BMP mip preparation failed");
        std::cout << "BMP admission, masks, palettes, orientation, alpha and rejection passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
