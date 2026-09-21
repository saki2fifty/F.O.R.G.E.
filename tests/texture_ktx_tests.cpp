#include "texture_ktx.hpp"
#include <cstdlib>
#include <cstring>
#include <gl_format.h>
#include <iostream>
#include <ktx.h>
#include <memory>
#include <source_location>
#include <vkformat_enum.h>
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
    throw std::runtime_error("Invalid KTX accepted at line " + std::to_string(where.line()));
}
void checked(KTX_error_code code) { require(code == KTX_SUCCESS, ktxErrorString(code)); }
struct Delete {
    void operator()(ktxTexture2* p) const {
        if (p)
            ktxTexture_Destroy(ktxTexture(p));
    }
};
std::uint64_t number(const std::vector<std::byte>& b, std::size_t at, unsigned count = 4) {
    std::uint64_t n = 0;
    for (unsigned i = 0; i < count; ++i)
        n |= std::uint64_t(std::to_integer<unsigned>(b.at(at + i))) << (8 * i);
    return n;
}
void put(std::vector<std::byte>& b, std::size_t at, std::uint64_t n, unsigned count = 4) {
    for (unsigned i = 0; i < count; ++i)
        b.at(at + i) = std::byte((n >> (8 * i)) & 255);
}
std::vector<std::byte> fixture(unsigned vk, unsigned w, unsigned h, unsigned depth, unsigned layers,
                               unsigned faces, unsigned mips, unsigned compression = 0) {
    ktxTextureCreateInfo info{};
    info.vkFormat = vk;
    info.baseWidth = w;
    info.baseHeight = h;
    info.baseDepth = std::max(depth, 1u);
    info.numDimensions = depth ? 3 : 2;
    info.numLayers = std::max(layers, 1u);
    info.isArray = layers != 0;
    info.numFaces = faces;
    info.numLevels = mips;
    ktxTexture2* raw = nullptr;
    checked(ktxTexture2_Create(&info, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &raw));
    std::unique_ptr<ktxTexture2, Delete> texture(raw);
    for (unsigned mip = 0; mip < mips; ++mip)
        for (unsigned layer = 0; layer < info.numLayers; ++layer)
            for (unsigned face = 0; face < faces; ++face)
                for (unsigned z = 0; z < std::max(1u, depth >> mip); ++z) {
                    const auto size = ktxTexture_GetImageSize(ktxTexture(raw), mip);
                    std::vector<unsigned char> bytes(
                        size, static_cast<unsigned char>(1 + mip + 7 * layer + 19 * face + 31 * z));
                    checked(ktxTexture_SetImageFromMemory(
                        ktxTexture(raw), mip, layer, depth ? z : face, bytes.data(), bytes.size()));
                }
    if (compression == 1)
        checked(ktxTexture2_DeflateZstd(raw, 3));

    unsigned char* bytes = nullptr;
    ktx_size_t length = 0;
    checked(ktxTexture_WriteToMemory(ktxTexture(raw), &bytes, &length));
    const auto* first = reinterpret_cast<const std::byte*>(bytes);
    std::vector<std::byte> result(first, first + length);
    std::free(bytes);
    return result;
}
std::vector<std::byte> fixture1(unsigned w, unsigned h, unsigned depth, unsigned layers,
                                unsigned faces, unsigned mips, bool orientation = false) {
    ktxTextureCreateInfo info{};
    info.glInternalformat = GL_R8;
    info.baseWidth = w;
    info.baseHeight = h;
    info.baseDepth = std::max(depth, 1u);
    info.numDimensions = depth ? 3 : 2;
    info.numLayers = std::max(layers, 1u);
    info.isArray = layers != 0;
    info.numFaces = faces;
    info.numLevels = mips;
    ktxTexture1* raw = nullptr;
    checked(ktxTexture1_Create(&info, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &raw));
    struct Delete1 {
        void operator()(ktxTexture1* p) const { ktxTexture_Destroy(ktxTexture(p)); }
    };
    std::unique_ptr<ktxTexture1, Delete1> native(raw);
    for (unsigned mip = 0; mip < mips; ++mip)
        for (unsigned layer = 0; layer < info.numLayers; ++layer)
            for (unsigned face = 0; face < faces; ++face)
                for (unsigned z = 0; z < std::max(1u, depth >> mip); ++z) {
                    std::vector<unsigned char> data(
                        std::size_t(std::max(1u, w >> mip)) * std::max(1u, h >> mip),
                        static_cast<unsigned char>(1 + mip + 7 * layer + 19 * face + 31 * z));
                    checked(ktxTexture_SetImageFromMemory(
                        ktxTexture(raw), mip, layer, depth ? z : face, data.data(), data.size()));
                }
    if (orientation) {
        constexpr char value[] = "S=r,T=d";
        checked(ktxHashList_AddKVPair(&raw->kvDataHead, KTX_ORIENTATION_KEY, sizeof(value), value));
    }
    unsigned char* bytes = nullptr;
    ktx_size_t length = 0;
    checked(ktxTexture_WriteToMemory(ktxTexture(raw), &bytes, &length));
    const auto* first = reinterpret_cast<const std::byte*>(bytes);
    std::vector<std::byte> result(first, first + length);
    std::free(bytes);
    return result;
}
TextureData pixels(bool srgb = true) {
    TextureData t;
    t.width = 4;
    t.height = 4;
    t.mips = 3;
    t.format = srgb ? TextureFormat::RGBA8Srgb : TextureFormat::RGBA8;
    t.alpha = TextureAlpha::Opaque;
    for (unsigned mip = 0; mip < 3; ++mip) {
        auto layout = texture_layout(t, mip);
        std::vector<std::byte> b(layout.bytes);
        for (std::size_t i = 0; i < b.size(); i += 4) {
            b[i] = std::byte{35};
            b[i + 1] = std::byte{179};
            b[i + 2] = std::byte{95};
            b[i + 3] = std::byte{255};
        }
        t.subresources.push_back(std::move(b));
    }
    return t;
}
} // namespace
int main() {
    try {
        for (auto dimension :
             {TextureDimension::D2, TextureDimension::D2Array, TextureDimension::Cube,
              TextureDimension::CubeArray, TextureDimension::D3}) {
            const auto faces =
                dimension == TextureDimension::Cube || dimension == TextureDimension::CubeArray
                    ? 6u
                    : 1u;
            const auto layers =
                dimension == TextureDimension::D2Array || dimension == TextureDimension::CubeArray
                    ? 2u
                    : 0u;
            const auto depth = dimension == TextureDimension::D3 ? 4u : 0u;
            for (unsigned compression : {0u, 1u}) {
                const auto bytes =
                    fixture(VK_FORMAT_R8_UNORM, 4, 4, depth, layers, faces, 3, compression);
                const auto t = import_texture_ktx2(bytes, TextureSemantic::Data);
                require(t.dimension == dimension && t.mips == 3 && t.format == TextureFormat::R8,
                        "KTX dimensional metadata lost");
                for (unsigned layer = 0; layer < std::max(1u, layers); ++layer)
                    for (unsigned face = 0; face < faces; ++face)
                        for (unsigned mip = 0; mip < 3; ++mip) {
                            const auto layout = texture_layout(t, mip);
                            const auto& data = t.subresources[(layer * faces + face) * 3 + mip];
                            for (unsigned z = 0; z < layout.depth; ++z)
                                require(std::to_integer<unsigned>(data[z * layout.slice_bytes]) ==
                                            1 + mip + 7 * layer + 19 * face + 31 * z,
                                        "KTX subresource ordering incorrect");
                        }
            }
        }
        for (unsigned faces : {1u, 6u})
            for (unsigned layers : {0u, 2u}) {
                const auto bytes = fixture1(4, 4, 0, layers, faces, 3);
                const auto t = import_texture_ktx1(bytes, TextureSemantic::Data);
                require(t.layers == std::max(1u, layers) &&
                            t.subresources.size() == t.layers * faces * 3,
                        "KTX1 array/cube lost subresources");
                for (std::size_t n = 0; n < bytes.size(); ++n) {
                    const std::vector<std::byte> truncated(bytes.begin(), bytes.begin() + n);
                    rejects([&] { import_texture_ktx1(truncated, TextureSemantic::Data); });
                }
            }
        const auto legacy_volume =
            import_texture_ktx1(fixture1(3, 5, 2, 0, 1, 2), TextureSemantic::Data);
        require(legacy_volume.depth == 2 && legacy_volume.subresources[0].size() == 30,
                "KTX1 volume/row-padding conversion failed");
        for (const auto format : {VK_FORMAT_BC1_RGB_UNORM_BLOCK, VK_FORMAT_BC7_UNORM_BLOCK}) {
            const auto compressed_volume =
                import_texture_ktx2(fixture(format, 4, 4, 4, 0, 1, 3, 0), TextureSemantic::Data);
            require(compressed_volume.dimension == TextureDimension::D3 &&
                        compressed_volume.depth == 4 && compressed_volume.mips == 3 &&
                        compressed_volume.subresources.front().size() ==
                            texture_layout(compressed_volume, 0).bytes,
                    "KTX compressed volume layout changed");
        }
        const auto oriented = fixture1(3, 5, 0, 0, 1, 1, true);
        require(import_texture_ktx1(oriented, TextureSemantic::Data).width == 3,
                "KTX1 canonical orientation failed");
        auto swapped = fixture1(3, 5, 0, 0, 1, 1);
        require(number(swapped, 60) == 0, "Endian fixture unexpectedly has metadata");
        for (unsigned at = 12; at < 68; at += 4)
            std::reverse(swapped.begin() + at, swapped.begin() + at + 4);
        require(import_texture_ktx1(swapped, TextureSemantic::Data).width == 3,
                "KTX1 endian conversion failed");
        // Tight KTX2 R8 rows are not the legacy four-byte GL row pitch.
        const auto odd = import_texture_ktx2(fixture(VK_FORMAT_R8_UNORM, 3, 5, 0, 0, 1, 2),
                                             TextureSemantic::Data);
        require(odd.subresources[0].size() == 15 && odd.subresources[1].size() == 2,
                "Odd KTX2 row layout incorrect");
        const auto bc = import_texture_ktx2(fixture(VK_FORMAT_BC7_UNORM_BLOCK, 7, 5, 0, 0, 1, 3),
                                            TextureSemantic::Data);
        require(bc.subresources[0].size() == 64 && bc.subresources[2].size() == 16,
                "NPOT compressed KTX block rows incorrect");
        const auto hdr = import_texture_ktx2(fixture(VK_FORMAT_BC6H_UFLOAT_BLOCK, 4, 4, 0, 0, 1, 1),
                                             TextureSemantic::HdrColor);
        require(hdr.format == TextureFormat::BC6Unsigned, "BC6H passthrough changed format");
        for (auto encoding : {BasisEncoding::Etc1s, BasisEncoding::Uastc}) {
            const auto input = pixels();
            const auto bytes = encode_texture_basis(input, encoding);
            require(bytes == encode_texture_basis(input, encoding),
                    "Basis profile output is not repeatable");
            const auto compressed = import_texture_ktx2(bytes, TextureSemantic::Color);
            require(compressed.format == TextureFormat::BC7Srgb && compressed.mips == 3,
                    "Basis desktop transcode failed");
            const auto rgba =
                import_texture_ktx2(bytes, TextureSemantic::Color, BasisTarget::Rgba8, true);
            require(rgba.format == TextureFormat::RGBA8Srgb && rgba.alpha == TextureAlpha::Opaque,
                    "Basis RGBA metadata incorrect");
            for (unsigned c = 0; c < 3; ++c)
                require(std::abs(std::to_integer<int>(rgba.subresources[0][c]) -
                                 std::to_integer<int>(input.subresources[0][c])) < 15,
                        "Basis channels were reordered");
            for (std::size_t n = 0; n < bytes.size(); ++n) {
                const std::vector<std::byte> truncated(bytes.begin(), bytes.begin() + n);
                rejects([&] { import_texture_ktx2(truncated, TextureSemantic::Color); });
            }
            auto bad = bytes;
            put(bad, 20, UINT32_MAX);
            rejects([&] { import_texture_ktx2(bad, TextureSemantic::Color); });
            bad = bytes;
            put(bad, 80, UINT64_MAX, 8);
            rejects([&] { import_texture_ktx2(bad, TextureSemantic::Color); });
            bad = bytes;
            put(bad, 88, UINT64_MAX, 8);
            rejects([&] { import_texture_ktx2(bad, TextureSemantic::Color); });
            bad = bytes;
            put(bad, 96, UINT64_MAX, 8);
            rejects([&] { import_texture_ktx2(bad, TextureSemantic::Color); });
            bad = bytes;
            put(bad, number(bytes, 48) + 10, 65535, 2);
            rejects([&] { import_texture_ktx2(bad, TextureSemantic::Color); });
            rejects([&] { import_texture_ktx2(bytes, TextureSemantic::Normal); });
            TextureLimits budget;
            budget.bytes = 64;
            rejects([&] {
                import_texture_ktx2(bytes, TextureSemantic::Color, BasisTarget::DesktopBc, false,
                                    budget);
            });
        }
        for (auto encoding : {BasisEncoding::Etc1s, BasisEncoding::Uastc}) {
            auto rg = pixels(false);
            rg.semantic = TextureSemantic::Data;
            rg.alpha = TextureAlpha::Straight;
            for (auto& bytes : rg.subresources)
                for (std::size_t i = 0; i < bytes.size(); i += 4) {
                    bytes[i] = bytes[i + 1] = bytes[i + 2] = std::byte{40};
                    bytes[i + 3] = std::byte{180};
                }
            auto bytes = encode_texture_basis(rg, encoding);
            const auto dfd = std::size_t(number(bytes, 48));
            if (encoding == BasisEncoding::Etc1s) {
                put(bytes, dfd + 31, KHR_DF_CHANNEL_ETC1S_RRR, 1);
                put(bytes, dfd + 47, KHR_DF_CHANNEL_ETC1S_GGG, 1);
            } else
                put(bytes, dfd + 31, KHR_DF_CHANNEL_UASTC_RRRG, 1);
            const auto linear =
                import_texture_ktx2(bytes, TextureSemantic::Data, BasisTarget::Rgba8);
            require(linear.format == TextureFormat::RG8 &&
                        std::abs(std::to_integer<int>(linear.subresources[0][0]) - 40) < 10 &&
                        std::abs(std::to_integer<int>(linear.subresources[0][1]) - 180) < 10,
                    "Basis packed RG channels incorrect");
            require(import_texture_ktx2(bytes, TextureSemantic::Normal).format ==
                        TextureFormat::BC5,
                    "Basis packed normal BC5 target incorrect");
        }
        auto normal = pixels(false);
        normal.semantic = TextureSemantic::Normal;
        const auto normal_basis = encode_texture_basis(normal, BasisEncoding::Uastc);
        require(import_texture_ktx2(normal_basis, TextureSemantic::Normal, BasisTarget::Rgba8, true)
                        .format == TextureFormat::RGBA8,
                "Data primaries were not encoded for glTF");
        rejects([&] {
            import_texture_ktx2(normal_basis, TextureSemantic::Color, BasisTarget::Rgba8, true);
        });
        // A standard unpacked UASTC RG descriptor retains green in G, not A.
        auto rg_basis = normal_basis;
        const auto rg_dfd = std::size_t(number(rg_basis, 48));
        put(rg_basis, rg_dfd + 31, KHR_DF_CHANNEL_UASTC_RG, 1);
        const auto rg8 = import_texture_ktx2(rg_basis, TextureSemantic::Data);
        require(rg8.format == TextureFormat::RG8 &&
                    std::abs(std::to_integer<int>(rg8.subresources[0][1]) - 179) < 15,
                "Unpacked UASTC green moved to alpha");
        for (auto encoding : {BasisEncoding::Etc1s, BasisEncoding::Uastc}) {
            auto bytes = encode_texture_basis(pixels(), encoding);
            const auto dfd = std::size_t(number(bytes, 48));
            auto damaged = bytes;
            put(damaged, dfd + 20, UINT64_MAX, 8);
            rejects([&] { import_texture_ktx2(damaged, TextureSemantic::Color); });
            damaged = bytes;
            put(damaged, dfd + 28, 65535, 2);
            rejects([&] { import_texture_ktx2(damaged, TextureSemantic::Color); });
            if (encoding == BasisEncoding::Etc1s) {
                const auto global = std::size_t(number(bytes, 64, 8));
                damaged = bytes;
                put(damaged, global + 4, UINT32_MAX);
                rejects([&] { import_texture_ktx2(damaged, TextureSemantic::Color); });
                damaged = bytes;
                put(damaged, global + 24, UINT32_MAX);
                rejects([&] { import_texture_ktx2(damaged, TextureSemantic::Color); });
            }
        }
        auto raw = fixture(VK_FORMAT_R8_UNORM, 4, 4, 0, 0, 1, 1);
        auto unsupported = raw;
        put(unsupported, 44, KTX_SS_ZLIB);
        rejects([&] { import_texture_ktx2(unsupported, TextureSemantic::Data); });
        auto bad = raw;
        put(bad, number(raw, 48) + 16, 255, 1);
        rejects([&] { import_texture_ktx2(bad, TextureSemantic::Data); });
        bad = raw;
        bad.push_back(std::byte{0});
        rejects([&] { import_texture_ktx2(bad, TextureSemantic::Data); });
        std::stop_source stop;
        stop.request_stop();
        rejects([&] {
            import_texture_ktx2(raw, TextureSemantic::Data, BasisTarget::DesktopBc, false, {},
                                stop.get_token());
        });
        std::cout << "KTX2 bounds/dimensions/mips/Basis/color/compression tests passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
