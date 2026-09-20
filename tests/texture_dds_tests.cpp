#include "texture_import.hpp"
#include <dxgiformat.h>
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
    throw std::runtime_error("Invalid DDS accepted at line " + std::to_string(where.line()));
}
unsigned fourcc(const char* s) {
    return unsigned(s[0]) | (unsigned(s[1]) << 8) | (unsigned(s[2]) << 16) | (unsigned(s[3]) << 24);
}
void put(std::vector<std::byte>& bytes, std::size_t at, unsigned n) {
    for (unsigned i = 0; i < 4; ++i)
        bytes.at(at + i) = std::byte((n >> (i * 8)) & 255);
}
std::vector<std::byte> fixture(unsigned dxgi, TextureFormat format,
                               TextureDimension dimension = TextureDimension::D2,
                               unsigned alpha = 0) {
    TextureData t;
    t.format = format;
    t.width = 4;
    t.height = 4;
    t.mips = 3;
    t.dimension = dimension;
    const bool cube =
        dimension == TextureDimension::Cube || dimension == TextureDimension::CubeArray;
    const bool volume = dimension == TextureDimension::D3;
    if (dimension == TextureDimension::D2Array || dimension == TextureDimension::CubeArray)
        t.layers = 2;
    if (volume)
        t.depth = 4;
    std::vector<std::byte> bytes(148);
    put(bytes, 0, fourcc("DDS "));
    put(bytes, 4, 124);
    put(bytes, 8, 0x21007 | (volume ? 0x800000 : 0));
    put(bytes, 12, t.height);
    put(bytes, 16, t.width);
    put(bytes, 24, volume ? t.depth : 0);
    put(bytes, 28, t.mips);
    put(bytes, 76, 32);
    put(bytes, 80, 4);
    put(bytes, 84, fourcc("DX10"));
    put(bytes, 108, 0x401008);
    put(bytes, 112, cube ? 0xfe00 : volume ? 0x200000 : 0);
    put(bytes, 128, dxgi);
    put(bytes, 132, volume ? 4 : 3);
    put(bytes, 136, cube ? 4 : 0);
    put(bytes, 140, t.layers);
    put(bytes, 144, alpha);
    for (unsigned face = 0; face < t.layers * (cube ? 6 : 1); ++face)
        for (unsigned mip = 0; mip < t.mips; ++mip) {
            const auto layout = texture_layout(t, mip);
            for (unsigned z = 0; z < layout.depth; ++z)
                bytes.insert(bytes.end(), layout.slice_bytes,
                             std::byte(1 + face * 5 + mip + z * 31));
        }
    return bytes;
}
std::vector<std::byte> legacy(std::vector<std::byte> bytes, unsigned code) {
    bytes.erase(bytes.begin() + 128, bytes.begin() + 148);
    put(bytes, 84, code);
    return bytes;
}
} // namespace
int main() {
    try {
        for (auto dimension :
             {TextureDimension::D2, TextureDimension::D2Array, TextureDimension::Cube,
              TextureDimension::CubeArray, TextureDimension::D3}) {
            const auto bytes = fixture(DXGI_FORMAT_R8_UNORM, TextureFormat::R8, dimension);
            const auto t = import_texture_dds(bytes, TextureSemantic::Data);
            require(t.dimension == dimension && t.mips == 3 && t.format == TextureFormat::R8,
                    "DDS dimension/mip metadata changed");
            for (unsigned i = 0; i < t.subresources.size(); ++i) {
                const auto layout = texture_layout(t, i % 3);
                for (unsigned z = 0; z < layout.depth; ++z)
                    require(t.subresources[i][z * layout.slice_bytes] ==
                                std::byte(1 + (i / 3) * 5 + (i % 3) + z * 31),
                            "DDS layer/face/mip/depth order changed");
            }
            for (std::size_t n = 0; n < bytes.size(); ++n) {
                const std::vector<std::byte> truncated(bytes.begin(), bytes.begin() + n);
                rejects([&] { import_texture_dds(truncated, TextureSemantic::Data); });
            }
        }
        for (auto [dxgi, format] : {std::pair{DXGI_FORMAT_BC1_UNORM, TextureFormat::BC1},
                                    {DXGI_FORMAT_BC3_UNORM, TextureFormat::BC3},
                                    {DXGI_FORMAT_BC4_SNORM, TextureFormat::BC4Snorm},
                                    {DXGI_FORMAT_BC5_UNORM, TextureFormat::BC5},
                                    {DXGI_FORMAT_BC6H_UF16, TextureFormat::BC6Unsigned},
                                    {DXGI_FORMAT_BC7_UNORM_SRGB, TextureFormat::BC7Srgb},
                                    {DXGI_FORMAT_R32G32B32A32_FLOAT, TextureFormat::RGBA32Float}}) {
            const auto semantic =
                format == TextureFormat::BC6Unsigned || format == TextureFormat::RGBA32Float
                    ? TextureSemantic::HdrColor
                    : TextureSemantic::Color;
            const auto t = import_texture_dds(fixture(dxgi, format), semantic);
            require(t.format == format && t.mips == 3, "DDS compressed/float format changed");
        }
        const auto premul = import_texture_dds(
            fixture(DXGI_FORMAT_R8G8B8A8_UNORM, TextureFormat::RGBA8, TextureDimension::D2, 2),
            TextureSemantic::Color);
        require(premul.alpha == TextureAlpha::Premultiplied, "DDS premultiplied metadata lost");
        const auto custom = import_texture_dds(
            fixture(DXGI_FORMAT_R8G8B8A8_UNORM, TextureFormat::RGBA8, TextureDimension::D2, 4),
            TextureSemantic::Data);
        require(custom.alpha == TextureAlpha::Custom &&
                    decode_texture(encode_texture(custom)).alpha == TextureAlpha::Custom,
                "DDS custom fourth-channel metadata lost");
        auto bgra = fixture(DXGI_FORMAT_B8G8R8A8_UNORM, TextureFormat::RGBA8);
        bgra[148] = std::byte{10};
        bgra[149] = std::byte{20};
        bgra[150] = std::byte{30};
        bgra[151] = std::byte{128};
        auto color = import_texture_dds(bgra, TextureSemantic::Color);
        require(color.subresources[0][0] == std::byte{30} &&
                    color.subresources[0][2] == std::byte{10} &&
                    color.subresources[0][3] == std::byte{128},
                "DDS BGRA swizzle failed");
        put(bgra, 128, DXGI_FORMAT_B8G8R8X8_UNORM);
        color = import_texture_dds(bgra, TextureSemantic::Color);
        require(color.alpha == TextureAlpha::Opaque && color.subresources[0][3] == std::byte{255},
                "DDS BGRX padding became alpha");
        auto bc2 = legacy(fixture(DXGI_FORMAT_BC2_UNORM, TextureFormat::BC2), fourcc("DXT2"));
        require(import_texture_dds(bc2, TextureSemantic::Color).alpha ==
                    TextureAlpha::Premultiplied,
                "Legacy DXT2 alpha lost");
        auto gray = legacy(fixture(DXGI_FORMAT_R8_UNORM, TextureFormat::R8), 0);
        put(gray, 80, 0x20000);
        put(gray, 88, 8);
        put(gray, 92, 255);
        const auto gray_color = import_texture_dds(gray, TextureSemantic::Color);
        require(gray_color.format == TextureFormat::RGBA8 &&
                    gray_color.subresources[0][0] == gray_color.subresources[0][1] &&
                    gray_color.subresources[0][3] == std::byte{255},
                "Legacy DDS luminance was not color-expanded");
        require(import_texture_dds(gray, TextureSemantic::Data).format == TextureFormat::R8,
                "DDS data channels expanded incorrectly");
        auto rgba = fixture(DXGI_FORMAT_R8G8B8A8_UNORM, TextureFormat::RGBA8);
        std::vector<std::byte> unaligned{std::byte{0}};
        unaligned.insert(unaligned.end(), rgba.begin(), rgba.end());
        require(import_texture_dds(std::span(unaligned).subspan(1), TextureSemantic::Color).width ==
                    4,
                "Unaligned DDS source was not copied safely");
        auto bad = rgba;
        put(bad, 140, UINT32_MAX);
        rejects([&] { import_texture_dds(bad, TextureSemantic::Color); });
        bad = rgba;
        put(bad, 28, 32);
        rejects([&] { import_texture_dds(bad, TextureSemantic::Color); });
        bad = rgba;
        put(bad, 128, DXGI_FORMAT_R8G8B8A8_TYPELESS);
        rejects([&] { import_texture_dds(bad, TextureSemantic::Color); });
        bad = rgba;
        put(bad, 16, UINT32_MAX);
        rejects([&] { import_texture_dds(bad, TextureSemantic::Color); });
        bad = rgba;
        bad.push_back(std::byte{0});
        rejects([&] { import_texture_dds(bad, TextureSemantic::Color); });
        auto srgb = fixture(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, TextureFormat::RGBA8Srgb);
        rejects([&] { import_texture_dds(srgb, TextureSemantic::Normal); });
        TextureLimits limits;
        limits.bytes = 100;
        rejects([&] { import_texture_dds(rgba, TextureSemantic::Color, limits); });
        std::stop_source stop;
        stop.request_stop();
        rejects([&] { import_texture_dds(rgba, TextureSemantic::Color, {}, stop.get_token()); });
        std::cout << "Native DDS bounds/mips/arrays/cubes/volume/alpha/channel tests passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
