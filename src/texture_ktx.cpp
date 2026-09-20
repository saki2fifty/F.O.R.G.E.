#include "texture_ktx.hpp"
#include <KHR/khr_df.h>
#include <basisu_comp.h>
#include <bit>
#include <cstring>
#include <ktx.h>
#include <memory>
#include <mutex>
#include <set>
#include <vk_format.h>
#include <vkformat_enum.h>

namespace forge::asset_detail {
namespace {
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
void cancelled(std::stop_token stop) { require(!stop.stop_requested(), "KTX operation cancelled"); }
void check(KTX_error_code code) {
    if (code != KTX_SUCCESS)
        throw std::runtime_error(std::string("KTX: ") + ktxErrorString(code));
}
struct Delete {
    void operator()(ktxTexture2* p) const {
        if (p)
            ktxTexture_Destroy(ktxTexture(p));
    }
};
using Texture = std::unique_ptr<ktxTexture2, Delete>;
constexpr std::array formats{VK_FORMAT_R8_UNORM,
                             VK_FORMAT_R8G8_UNORM,
                             VK_FORMAT_R8G8B8A8_UNORM,
                             VK_FORMAT_R8G8B8A8_SRGB,
                             VK_FORMAT_R16_UNORM,
                             VK_FORMAT_R16G16_UNORM,
                             VK_FORMAT_R16G16B16A16_UNORM,
                             VK_FORMAT_R16_SFLOAT,
                             VK_FORMAT_R16G16_SFLOAT,
                             VK_FORMAT_R16G16B16A16_SFLOAT,
                             VK_FORMAT_R32_SFLOAT,
                             VK_FORMAT_R32G32_SFLOAT,
                             VK_FORMAT_R32G32B32A32_SFLOAT,
                             VK_FORMAT_BC1_RGBA_UNORM_BLOCK,
                             VK_FORMAT_BC1_RGBA_SRGB_BLOCK,
                             VK_FORMAT_BC2_UNORM_BLOCK,
                             VK_FORMAT_BC2_SRGB_BLOCK,
                             VK_FORMAT_BC3_UNORM_BLOCK,
                             VK_FORMAT_BC3_SRGB_BLOCK,
                             VK_FORMAT_BC4_UNORM_BLOCK,
                             VK_FORMAT_BC4_SNORM_BLOCK,
                             VK_FORMAT_BC5_UNORM_BLOCK,
                             VK_FORMAT_BC5_SNORM_BLOCK,
                             VK_FORMAT_BC6H_UFLOAT_BLOCK,
                             VK_FORMAT_BC6H_SFLOAT_BLOCK,
                             VK_FORMAT_BC7_UNORM_BLOCK,
                             VK_FORMAT_BC7_SRGB_BLOCK};
static_assert(formats.size() == unsigned(TextureFormat::BC7Srgb) + 1);
TextureFormat format_of(unsigned vk) {
    // BC1 RGB shares storage with BC1 RGBA; its alpha is explicitly opaque.
    if (vk == VK_FORMAT_BC1_RGB_UNORM_BLOCK)
        return TextureFormat::BC1;
    if (vk == VK_FORMAT_BC1_RGB_SRGB_BLOCK)
        return TextureFormat::BC1Srgb;
    for (unsigned i = 0; i < formats.size(); ++i)
        if (unsigned(formats[i]) == vk)
            return TextureFormat(i);
    throw std::runtime_error("KTX format is outside the supported texture profile");
}
struct Bytes {
    std::span<const std::byte> data;
    bool big_endian = false;
    std::span<const std::byte> span(std::uint64_t offset, std::uint64_t size) const {
        require(offset <= data.size() && size <= data.size() - offset, "Truncated KTX range");
        return data.subspan(std::size_t(offset), std::size_t(size));
    }
    std::uint64_t number(std::size_t at, unsigned count = 4) const {
        const auto s = span(at, count);
        std::uint64_t n = 0;
        for (unsigned i = 0; i < count; ++i)
            n |= std::uint64_t(std::to_integer<unsigned>(s[i]))
                 << (8 * (big_endian ? count - i - 1 : i));
        return n;
    }
};
struct Level {
    std::uint64_t offset, bytes, inflated;
};
struct Admission {
    TextureData texture;
    unsigned vk = 0, scheme = 0, channels = 4;
    bool basis = false, packed_rg = false, srgb = false;
    std::vector<Level> levels;
};
// Bounded KVD parsing protects the native hash-list parser and gives metadata
// that affects sampling an explicit supported contract, before image decoding.
void metadata(Bytes b, unsigned dimensions, bool gltf, bool legacy = false) {
    std::set<std::string> keys;
    std::size_t at = 0;
    while (at < b.data.size()) {
        require(keys.size() < 1024, "Too many KTX metadata entries");
        const auto length = b.number(at);
        at += 4;
        const auto entry = b.span(at, length);
        const auto end = std::find(entry.begin(), entry.end(), std::byte{0});
        require(end != entry.end() && end != entry.begin(), "Invalid KTX metadata key");
        const auto key_length = std::size_t(end - entry.begin());
        require(key_length <= 1024, "KTX metadata key too long");
        const std::string key(reinterpret_cast<const char*>(entry.data()), key_length);
        require(keys.insert(key).second, "Duplicate KTX metadata key");
        auto value = entry.subspan(key_length + 1);
        if (!value.empty() && value.back() == std::byte{0})
            value = value.first(value.size() - 1);
        const std::string_view text(reinterpret_cast<const char*>(value.data()), value.size());
        if (key == KTX_ORIENTATION_KEY) {
            require(!legacy || (!entry.empty() && entry.back() == std::byte{0}),
                    "KTX1 orientation must be terminated");
            require(text == (legacy ? (dimensions == 3 ? "S=r,T=d,R=i" : "S=r,T=d")
                                    : (dimensions == 3 ? "rdi" : "rd")),
                    "KTX orientation needs explicit conversion to right/down/in");
        }
        if (key == KTX_SWIZZLE_KEY)
            require(text == "rgba", "KTX channel swizzle needs explicit conversion");
        if (gltf)
            require(key != "KTXanimData", "glTF Basis texture cannot be a video");
        at += std::size_t(length);
        const auto padding = (4 - at % 4) % 4;
        b.span(at, padding);
        at += padding;
    }
}
Admission admit(Bytes b, TextureSemantic semantic, TextureLimits limits, bool gltf) {
    // KTX2 file signature (also KTX2_IDENTIFIER_REF in pinned lib/ktxint.h).
    constexpr unsigned char magic[12] = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32,
                                         0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
    require(b.data.size() <= limits.bytes && b.data.size() >= 80 &&
                std::memcmp(b.data.data(), magic, 12) == 0,
            "Invalid or oversized KTX2 source");
    Admission a;
    a.vk = unsigned(b.number(12));
    const auto type_size = b.number(16);
    auto& t = a.texture;
    t.width = unsigned(b.number(20));
    t.height = unsigned(b.number(24));
    const auto depth = unsigned(b.number(28)), layers = unsigned(b.number(32)),
               faces = unsigned(b.number(36));
    t.depth = std::max(1u, depth);
    t.layers = std::max(1u, layers);
    t.mips = std::max(1u, unsigned(b.number(40)));
    t.semantic = semantic;
    a.scheme = unsigned(b.number(44));
    require(faces == 1 || faces == 6, "KTX face count must be one or six");
    require(!depth || (!layers && faces == 1), "KTX volume cannot also be an array/cubemap");
    t.dimension = depth        ? TextureDimension::D3
                  : faces == 6 ? (layers ? TextureDimension::CubeArray : TextureDimension::Cube)
                  : layers     ? TextureDimension::D2Array
                               : TextureDimension::D2;
    a.basis = a.vk == VK_FORMAT_UNDEFINED;
    t.format = a.basis ? TextureFormat::RGBA8 : format_of(a.vk);
    // RGBA8 bounds the native Basis output even when the chosen target is BC.
    validate_texture_metadata(t, limits);
    require(!a.basis || !depth, "Basis volume transcoding is outside this profile");
    require(a.scheme <= KTX_SS_ZSTD,
            "Unsupported KTX supercompression; zlib is excluded by the codec safety profile");
    require(a.basis || a.scheme != KTX_SS_BASIS_LZ, "BasisLZ requires an undefined Vulkan format");
    const auto f = texture_format_info(t.format);
    require(type_size == (a.basis || f.compressed ? 1 : f.block_bytes / f.channels),
            "KTX scalar size disagrees with format");
    const auto index_end = 80 + 24 * std::size_t(t.mips);
    b.span(80, index_end - 80);
    const auto dfd_at = b.number(48), dfd_size = b.number(52), kvd_at = b.number(56),
               kvd_size = b.number(60), sgd_at = b.number(64, 8), sgd_size = b.number(72, 8);
    require(dfd_at == index_end && dfd_size >= 44 && dfd_size <= 4096,
            "Unsupported KTX data format descriptor extent");
    Bytes d{b.span(dfd_at, dfd_size)};
    require(d.number(0) == dfd_size && d.number(4) == 0 && d.number(8, 2) == 2 &&
                d.number(10, 2) == dfd_size - 4 && (dfd_size - 28) % 16 == 0,
            "Invalid KTX basic data format descriptor");
    const auto model = unsigned(d.number(12, 1)), primaries = unsigned(d.number(13, 1)),
               transfer = unsigned(d.number(14, 1)), flags = unsigned(d.number(15, 1));
    require(transfer == KHR_DF_TRANSFER_LINEAR || transfer == KHR_DF_TRANSFER_SRGB,
            "KTX transfer function needs explicit color conversion");
    require(primaries == KHR_DF_PRIMARIES_UNSPECIFIED || primaries == KHR_DF_PRIMARIES_BT709,
            "KTX color primaries need explicit conversion");
    require(flags <= KHR_DF_FLAG_ALPHA_PREMULTIPLIED, "Unsupported KTX data format flags");
    a.srgb = transfer == KHR_DF_TRANSFER_SRGB;
    require(!a.srgb || semantic == TextureSemantic::Color,
            "KTX sRGB storage contradicts data/normal/HDR semantic");
    const auto samples = (dfd_size - 28) / 16;
    if (a.basis) {
        const bool etc = model == KHR_DF_MODEL_ETC1S;
        require(etc || model == KHR_DF_MODEL_UASTC, "Unsupported KTX universal codec");
        require(etc ? a.scheme == KTX_SS_BASIS_LZ
                    : (a.scheme == KTX_SS_NONE || a.scheme == KTX_SS_ZSTD),
                "KTX codec/supercompression mismatch");
        require(d.number(16) == 0x00000303 && samples <= (etc ? 2u : 1u),
                "Invalid Basis block shape/sample count");
        // Accept current sized planes and the older all-zero supercompressed
        // convention emitted by this pin's native Basis KTX2 writer.
        const auto planes = d.number(20, 8);
        require(planes == 0 || planes == (etc ? (samples == 2 ? 0x0808u : 8u) : 16u),
                "Invalid Basis plane layout");
        for (std::size_t sample = 0; sample < samples; ++sample) {
            const auto at = 28 + 16 * sample;
            require(d.number(at, 2) == (etc ? sample * 64 : 0) &&
                        d.number(at + 2, 1) == (etc ? 63 : 127) && d.number(at + 4) == 0 &&
                        d.number(at + 8) == 0 && d.number(at + 12) == UINT32_MAX,
                    "Invalid Basis sample layout");
        }
        const auto c0 = unsigned(d.number(31, 1));
        if (etc) {
            require(c0 == KHR_DF_CHANNEL_ETC1S_RGB || c0 == KHR_DF_CHANNEL_ETC1S_RRR,
                    "Unsupported ETC1S primary channel");
            a.channels = c0 == KHR_DF_CHANNEL_ETC1S_RRR ? 1 : 3;
            if (samples == 2) {
                const auto c1 = d.number(47, 1);
                require(c1 == KHR_DF_CHANNEL_ETC1S_AAA || c1 == KHR_DF_CHANNEL_ETC1S_GGG,
                        "Unsupported ETC1S secondary channel");
                a.packed_rg = c1 == KHR_DF_CHANNEL_ETC1S_GGG;
                require(!a.packed_rg || a.channels == 1, "Invalid ETC1S red/green pairing");
                a.channels = a.packed_rg ? 2 : 4;
            }
        } else {
            switch (c0) {
            case KHR_DF_CHANNEL_UASTC_RGB:
                a.channels = 3;
                break;
            case KHR_DF_CHANNEL_UASTC_RGBA:
                a.channels = 4;
                break;
            case KHR_DF_CHANNEL_UASTC_RRR:
                a.channels = 1;
                break;
            case KHR_DF_CHANNEL_UASTC_RG:
                a.channels = 2;
                break;
            case KHR_DF_CHANNEL_UASTC_RRRG:
                a.channels = 2;
                a.packed_rg = true;
                break;
            default:
                throw std::runtime_error("Unsupported UASTC channel mapping");
            }
        }
        require(!a.srgb || a.channels >= 3, "Red/RG Basis storage must be linear");
        require(semantic != TextureSemantic::Normal || a.channels >= 2,
                "Normal texture requires two or more channels");
        require(semantic != TextureSemantic::HdrColor, "Selected Basis codecs are LDR");
    } else {
        // Compare the exact native descriptor for the admitted VkFormat. Only
        // primaries and alpha mode are metadata choices; layout must agree.
        ktxTextureCreateInfo ci{};
        ci.vkFormat = a.vk;
        ci.baseWidth = 4;
        ci.baseHeight = 4;
        ci.baseDepth = 1;
        ci.numDimensions = 2;
        ci.numLevels = 1;
        ci.numLayers = 1;
        ci.numFaces = 1;
        ktxTexture2* raw = nullptr;
        check(ktxTexture2_Create(&ci, KTX_TEXTURE_CREATE_NO_STORAGE, &raw));
        Texture canonical(raw);
        require(raw->pDfd[0] == dfd_size, "KTX descriptor disagrees with Vulkan format");
        const auto* native = reinterpret_cast<const std::byte*>(raw->pDfd);
        for (std::size_t i = 0; i < dfd_size; ++i)
            if (i != 13 && i != 15)
                require(d.data[i] == native[i], "KTX descriptor disagrees with Vulkan format");
        a.channels = f.channels;
    }
    require(!kvd_size || (kvd_at == dfd_at + dfd_size && kvd_size <= 4 * 1024 * 1024),
            "Invalid KTX metadata extent");
    require(kvd_size || !kvd_at, "Empty KTX metadata has an offset");
    if (kvd_size)
        metadata(Bytes{b.span(kvd_at, kvd_size)}, depth ? 3 : 2, gltf);
    require(sgd_size || !sgd_at, "Empty KTX global data has an offset");
    require(a.scheme == KTX_SS_BASIS_LZ ? sgd_size >= 20 : !sgd_size,
            "Invalid KTX global data extent");
    std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges{{0, index_end},
                                                                {dfd_at, dfd_at + dfd_size}};
    if (kvd_size)
        ranges.emplace_back(kvd_at, kvd_at + kvd_size);
    if (sgd_size) {
        b.span(sgd_at, sgd_size);
        require(sgd_at % 8 == 0, "Unaligned KTX global data");
        ranges.emplace_back(sgd_at, sgd_at + sgd_size);
    }
    std::size_t total = 0;
    for (unsigned mip = 0; mip < t.mips; ++mip) {
        const auto at = 80 + 24 * std::size_t(mip);
        Level level{b.number(at, 8), b.number(at + 8, 8), b.number(at + 16, 8)};
        require(level.bytes && level.inflated <= limits.bytes, "Invalid KTX level length");
        b.span(level.offset, level.bytes);
        require(mip == 0 || level.offset + level.bytes <= a.levels.back().offset,
                "KTX levels must be stored smallest first");
        auto layout = texture_layout(t, mip);
        const auto expected = a.basis ? ((std::size_t(layout.width) + 3) / 4) *
                                            ((layout.height + 3) / 4) * 16 * t.layers * faces
                                      : layout.bytes * t.layers * faces;
        if (a.scheme == KTX_SS_BASIS_LZ)
            require(level.inflated == 0, "BasisLZ inflated length must be zero");
        else
            require(level.inflated == expected &&
                        (a.scheme != KTX_SS_NONE || level.bytes == expected),
                    "KTX level size disagrees with dimensions/format");
        require(expected <= limits.bytes - total, "KTX decoded storage exceeds budget");
        total += expected;
        ranges.emplace_back(level.offset, level.offset + level.bytes);
        a.levels.push_back(level);
    }
    std::sort(ranges.begin(), ranges.end());
    for (std::size_t i = 1; i < ranges.size(); ++i)
        require(ranges[i - 1].second <= ranges[i].first, "Overlapping KTX ranges");
    require(ranges.back().second == b.data.size(), "Unexpected trailing KTX bytes");
    if (a.scheme == KTX_SS_BASIS_LZ) {
        Bytes s{b.span(sgd_at, sgd_size)};
        const auto images = std::size_t(t.layers) * faces * t.mips;
        require(sgd_size >= 20 + images * 20, "Truncated ETC1S image descriptors");
        std::uint64_t length = 20 + images * 20;
        for (unsigned at : {4u, 8u, 12u, 16u})
            length += s.number(at);
        require(length == sgd_size && s.number(0, 2) && s.number(2, 2) && !s.number(16),
                "Invalid ETC1S codebook sizes");
        for (std::size_t i = 0; i < images; ++i) {
            const auto at = 20 + i * 20;
            require(s.number(at) == 0, "Basis video frames are outside the texture profile");
            const auto& level = a.levels[i / (t.layers * faces)];
            const auto rgb = s.number(at + 4), rgb_size = s.number(at + 8),
                       alpha = s.number(at + 12), alpha_size = s.number(at + 16);
            require(rgb_size && rgb <= level.bytes && rgb_size <= level.bytes - rgb &&
                        alpha <= level.bytes && alpha_size <= level.bytes - alpha,
                    "ETC1S image slice exceeds level");
            require((a.channels == 2 || a.channels == 4) ? alpha_size != 0 : alpha_size == 0,
                    "ETC1S alpha slice disagrees with channels");
        }
    }
    if (gltf) {
        require((semantic == TextureSemantic::Color) == a.srgb,
                "glTF Basis transfer function must match the material semantic");
        require(a.basis && t.dimension == TextureDimension::D2 && t.width % 4 == 0 &&
                    t.height % 4 == 0 &&
                    (t.mips == 1 || t.mips == std::bit_width(std::max(t.width, t.height))),
                "KTX violates glTF Basis dimensions/mips");
        require(!a.packed_rg || model == KHR_DF_MODEL_ETC1S,
                "UASTC RRRG is not a glTF Basis channel layout");
        require(primaries == (a.srgb ? KHR_DF_PRIMARIES_BT709 : KHR_DF_PRIMARIES_UNSPECIFIED) &&
                    !flags,
                "KTX violates glTF Basis color/alpha metadata");
    }
    t.alpha = a.channels < 4 || a.vk == VK_FORMAT_BC1_RGB_UNORM_BLOCK ||
                      a.vk == VK_FORMAT_BC1_RGB_SRGB_BLOCK
                  ? TextureAlpha::Opaque
              : flags ? TextureAlpha::Premultiplied
                      : TextureAlpha::Straight;
    return a;
}
} // namespace
TextureData import_texture_ktx2(std::span<const std::byte> bytes, TextureSemantic semantic,
                                BasisTarget target, bool gltf, TextureLimits limits,
                                std::stop_token stop) {
    static_assert(std::endian::native == std::endian::little);
    cancelled(stop);
    require(unsigned(target) <= unsigned(BasisTarget::Rgba8), "Invalid Basis target");
    auto a = admit(Bytes{bytes}, semantic, limits, gltf);
    ktxTexture2* raw = nullptr;
    check(ktxTexture2_CreateFromMemory(reinterpret_cast<const ktx_uint8_t*>(bytes.data()),
                                       bytes.size(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &raw));
    Texture native(raw);
    cancelled(stop);
    bool unpack_rg = false, unpack_r = false;
    if (a.basis) {
        auto output = KTX_TTF_RGBA32;
        if (target == BasisTarget::DesktopBc) {
            if (a.channels == 1)
                output = KTX_TTF_BC4_R;
            else if (a.channels == 2 && a.packed_rg)
                output = KTX_TTF_BC5_RG;
            else if (a.channels >= 3)
                output = KTX_TTF_BC7_RGBA;
        }
        check(ktxTexture2_TranscodeBasis(raw, output, 0));
        unpack_rg = output == KTX_TTF_RGBA32 && a.channels == 2;
        unpack_r = output == KTX_TTF_RGBA32 && a.channels == 1;
    }
    auto& t = a.texture;
    t.format = format_of(raw->vkFormat);
    validate_texture_metadata(t, limits);
    require(raw->dataSize <= limits.bytes, "Native KTX decoded payload exceeds budget");
    const auto faces = raw->numFaces;
    for (unsigned layer = 0; layer < t.layers; ++layer)
        for (unsigned face = 0; face < faces; ++face)
            for (unsigned mip = 0; mip < t.mips; ++mip) {
                cancelled(stop);
                const auto layout = texture_layout(t, mip);
                ktx_size_t offset = 0;
                check(ktxTexture_GetImageOffset(ktxTexture(raw), mip, layer, face, &offset));
                require(offset <= raw->dataSize && layout.bytes <= raw->dataSize - offset,
                        "Native KTX subresource exceeds decoded payload");
                const auto* first = reinterpret_cast<const std::byte*>(raw->pData) + offset;
                t.subresources.emplace_back(first, first + layout.bytes);
            }
    if (unpack_r || unpack_rg) {
        for (auto& data : t.subresources) {
            std::vector<std::byte> channels;
            channels.reserve(data.size() / 4 * (unpack_rg ? 2 : 1));
            for (std::size_t i = 0; i < data.size(); i += 4) {
                channels.push_back(data[i]);
                if (unpack_rg)
                    channels.push_back(data[i + (a.packed_rg ? 3 : 1)]);
            }
            data = std::move(channels);
        }
        t.format = unpack_rg ? TextureFormat::RG8 : TextureFormat::R8;
    }
    validate_texture(t, limits);
    return std::move(t);
}
TextureData import_texture_ktx1(std::span<const std::byte> bytes, TextureSemantic semantic,
                                TextureLimits limits, std::stop_token stop) {
    cancelled(stop);
    constexpr unsigned char magic[12] = KTX_IDENTIFIER_REF;
    require(bytes.size() >= 64 && bytes.size() <= limits.bytes && bytes.size() <= UINT32_MAX &&
                std::memcmp(bytes.data(), magic, 12) == 0,
            "Invalid or oversized KTX1 source");
    Bytes b{bytes};
    require(b.number(12) == 0x04030201 || b.number(12) == 0x01020304, "Invalid KTX1 endian marker");
    b.big_endian = b.number(12) == 0x01020304;
    const auto gl = unsigned(b.number(28));
    const auto vk = vkGetFormatFromOpenGLInternalFormat(gl);
    TextureData t;
    t.format = format_of(vk);
    t.semantic = semantic;
    t.width = unsigned(b.number(36));
    t.height = unsigned(b.number(40));
    const auto depth = unsigned(b.number(44)), layers = unsigned(b.number(48)),
               faces = unsigned(b.number(52));
    t.depth = std::max(depth, 1u);
    t.layers = std::max(layers, 1u);
    t.mips = std::max(unsigned(b.number(56)), 1u);
    require(faces == 1 || faces == 6, "Invalid KTX1 face count");
    require(!depth || (!layers && faces == 1), "KTX1 volume cannot be an array/cubemap");
    t.dimension = depth        ? TextureDimension::D3
                  : faces == 6 ? (layers ? TextureDimension::CubeArray : TextureDimension::Cube)
                  : layers     ? TextureDimension::D2Array
                               : TextureDimension::D2;
    validate_texture_metadata(t, limits);
    const auto f = texture_format_info(t.format);
    require(b.number(16) == (f.compressed ? 0 : glGetTypeFromInternalFormat(gl)) &&
                b.number(20) == (f.compressed ? 1 : f.block_bytes / f.channels) &&
                b.number(24) == (f.compressed ? 0 : glGetFormatFromInternalFormat(gl)) &&
                b.number(32) == glGetFormatFromInternalFormat(gl),
            "KTX1 GL format/type fields disagree");
    const auto kvd = b.number(60);
    require(kvd <= 4 * 1024 * 1024, "KTX1 metadata exceeds budget");
    metadata(Bytes{b.span(64, kvd), b.big_endian}, depth ? 3 : 2, false, true);
    std::size_t at = 64 + std::size_t(kvd), total = 0;
    for (unsigned mip = 0; mip < t.mips; ++mip) {
        const auto image_size = b.number(at);
        at += 4;
        const auto layout = texture_layout(t, mip);
        const auto row = (layout.row_bytes + 3) / 4 * 4;
        const auto rows = f.compressed ? (std::size_t(layout.height) + 3) / 4 : layout.height;
        const auto face_bytes = row * rows * layout.depth;
        const auto expected = !layers && faces == 6 ? face_bytes : face_bytes * t.layers * faces;
        require(image_size == expected, "KTX1 mip size disagrees with dimensions/format");
        const auto count = !layers && faces == 6 ? 6u : 1u;
        for (unsigned face = 0; face < count; ++face) {
            require(expected <= limits.bytes - total, "KTX1 padded storage exceeds budget");
            b.span(at, expected);
            at += expected;
            total += expected;
        }
    }
    require(at == bytes.size(), "Unexpected trailing KTX1 bytes");
    struct Delete1 {
        void operator()(ktxTexture1* p) const {
            if (p)
                ktxTexture_Destroy(ktxTexture(p));
        }
    };
    ktxTexture1* raw = nullptr;
    check(ktxTexture1_CreateFromMemory(reinterpret_cast<const ktx_uint8_t*>(bytes.data()),
                                       bytes.size(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &raw));
    std::unique_ptr<ktxTexture1, Delete1> native(raw);
    cancelled(stop);
    // Metadata affecting interpretation has already been validated. The cooked
    // texture does not carry arbitrary source KVD; the source file remains intact.
    // Native replacement of KTXwriter/orientation leaks detached entries at this
    // pin. Reconstruct this temporary conversion object's whole public hash list.
    ktxHashList_Destruct(&raw->kvDataHead);
    ktxHashList_Construct(&raw->kvDataHead);
    // Native conversion removes KTX1 row padding and converts orientation metadata.
    // KTX2 admission then uses the same typed format/semantic/mip contract.
    unsigned char* output = nullptr;
    ktx_size_t length = 0;
    check(ktxTexture1_WriteKTX2ToMemory(raw, &output, &length));
    std::unique_ptr<unsigned char, decltype(&std::free)> converted(output, &std::free);
    return import_texture_ktx2({reinterpret_cast<const std::byte*>(output), length}, semantic,
                               BasisTarget::DesktopBc, false, limits, stop);
}
std::vector<std::byte> encode_texture_basis(const TextureData& t, BasisEncoding encoding,
                                            TextureLimits limits, std::stop_token stop) {
    cancelled(stop);
    validate_texture(t, limits);
    require(unsigned(encoding) <= unsigned(BasisEncoding::Uastc), "Invalid Basis encoder");
    require(t.dimension == TextureDimension::D2 &&
                (t.format == TextureFormat::RGBA8 || t.format == TextureFormat::RGBA8Srgb),
            "Basis encoding requires prepared RGBA8 2D mips");
    require(t.alpha != TextureAlpha::Premultiplied && t.alpha != TextureAlpha::Custom,
            "Basis encoding needs an explicit premultiplied/custom alpha metadata policy");
    // Upstream initialization/global codec state is serialized within the worker.
    static std::mutex mutex;
    std::lock_guard lock(mutex);
    static const bool initialized = [] {
        basisu::basisu_encoder_init(false);
        return true;
    }();
    (void)initialized;
    basisu::job_pool jobs(1);
    basisu::basis_compressor_params p;
    p.m_pJob_pool = &jobs;
    p.m_read_source_images = false;
    p.m_write_output_basis_files = false;
    p.m_status_output = false;
    p.m_multithreading = false;
    p.m_uastc = encoding == BasisEncoding::Uastc;
    p.m_perceptual = t.format == TextureFormat::RGBA8Srgb;
    p.m_quality_level = 128;
    p.m_compression_level = 2;
    p.m_create_ktx2_file = true;
    p.m_ktx2_srgb_transfer_func = p.m_perceptual;
    p.m_source_images.resize(1);
    p.m_source_mipmap_images.resize(1);
    p.m_source_mipmap_images[0].resize(t.mips - 1);
    for (unsigned mip = 0; mip < t.mips; ++mip) {
        cancelled(stop);
        const auto layout = texture_layout(t, mip);
        auto& image = mip ? p.m_source_mipmap_images[0][mip - 1] : p.m_source_images[0];
        image.resize(layout.width, layout.height);
        std::memcpy(image.get_ptr(), t.subresources[mip].data(), layout.bytes);
    }
    if (t.semantic == TextureSemantic::Normal) {
        p.m_no_endpoint_rdo = true;
        p.m_no_selector_rdo = true;
    }
    basisu::basis_compressor compressor;
    require(compressor.init(p) && compressor.process() == basisu::basis_compressor::cECSuccess,
            "Basis encoding failed");
    cancelled(stop);
    const auto& output = compressor.get_output_ktx2_file();
    require(output.size() <= limits.bytes, "Encoded KTX2 exceeds budget");
    const auto* first = reinterpret_cast<const std::byte*>(output.data());
    std::vector<std::byte> result(first, first + output.size());
    (void)admit(Bytes{result}, t.semantic, limits, false);
    if (t.semantic == TextureSemantic::Data || t.semantic == TextureSemantic::Normal) {
        ktxTexture2* raw = nullptr;
        check(ktxTexture2_CreateFromMemory(reinterpret_cast<const ktx_uint8_t*>(result.data()),
                                           result.size(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
                                           &raw));
        Texture metadata_owner(raw);
        check(ktxTexture2_SetPrimaries(raw, KHR_DF_PRIMARIES_UNSPECIFIED));
        // This is our encoder output, containing only its writer/alignment KVD.
        // DeleteEntry at this revision detaches without freeing; avoid that
        // writer replacement path using the public whole-list lifetime APIs.
        ktxHashList_Destruct(&raw->kvDataHead);
        ktxHashList_Construct(&raw->kvDataHead);
        constexpr char provenance[] =
            "FORGE; KTX4.4.2/Basis; one thread; UASTC RDO off; ETC1S Q128 C2";
        check(ktxHashList_AddKVPair(&raw->kvDataHead, "FORGEencoder", sizeof(provenance),
                                    provenance));
        unsigned char* bytes = nullptr;
        ktx_size_t size = 0;
        check(ktxTexture_WriteToMemory(ktxTexture(raw), &bytes, &size));
        std::unique_ptr<unsigned char, decltype(&std::free)> rewritten(bytes, &std::free);
        require(size <= limits.bytes, "Encoded KTX2 metadata exceeds budget");
        const auto* start = reinterpret_cast<const std::byte*>(bytes);
        result.assign(start, start + size);
        (void)admit(Bytes{result}, t.semantic, limits, false);
    }
    return result;
}
} // namespace forge::asset_detail
