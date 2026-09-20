#include "texture_diligent.hpp"
#include "texture_import.hpp"
#include <RefCntAutoPtr.hpp>
#include <TextureUtilities.h>
#include <cstring>
#include <dxgiformat.h>

namespace forge::asset_detail {
namespace {
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
struct Header {
    std::span<const std::byte> bytes;
    unsigned at(std::size_t offset) const {
        require(offset <= bytes.size() && bytes.size() - offset >= 4, "Truncated DDS header");
        unsigned n = 0;
        for (unsigned i = 0; i < 4; ++i)
            n |= std::to_integer<unsigned>(bytes[offset + i]) << (i * 8);
        return n;
    }
};
constexpr unsigned fourcc(const char* s) {
    return unsigned(s[0]) | (unsigned(s[1]) << 8) | (unsigned(s[2]) << 16) | (unsigned(s[3]) << 24);
}
constexpr std::array dxgi_formats{DXGI_FORMAT_R8_UNORM,           DXGI_FORMAT_R8G8_UNORM,
                                  DXGI_FORMAT_R8G8B8A8_UNORM,     DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
                                  DXGI_FORMAT_R16_UNORM,          DXGI_FORMAT_R16G16_UNORM,
                                  DXGI_FORMAT_R16G16B16A16_UNORM, DXGI_FORMAT_R16_FLOAT,
                                  DXGI_FORMAT_R16G16_FLOAT,       DXGI_FORMAT_R16G16B16A16_FLOAT,
                                  DXGI_FORMAT_R32_FLOAT,          DXGI_FORMAT_R32G32_FLOAT,
                                  DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_BC1_UNORM,
                                  DXGI_FORMAT_BC1_UNORM_SRGB,     DXGI_FORMAT_BC2_UNORM,
                                  DXGI_FORMAT_BC2_UNORM_SRGB,     DXGI_FORMAT_BC3_UNORM,
                                  DXGI_FORMAT_BC3_UNORM_SRGB,     DXGI_FORMAT_BC4_UNORM,
                                  DXGI_FORMAT_BC4_SNORM,          DXGI_FORMAT_BC5_UNORM,
                                  DXGI_FORMAT_BC5_SNORM,          DXGI_FORMAT_BC6H_UF16,
                                  DXGI_FORMAT_BC6H_SF16,          DXGI_FORMAT_BC7_UNORM,
                                  DXGI_FORMAT_BC7_UNORM_SRGB};
static_assert(dxgi_formats.size() == unsigned(TextureFormat::BC7Srgb) + 1);
TextureFormat format_of(unsigned dxgi) {
    if (dxgi == DXGI_FORMAT_B8G8R8A8_UNORM || dxgi == DXGI_FORMAT_B8G8R8X8_UNORM)
        return TextureFormat::RGBA8;
    if (dxgi == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || dxgi == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB)
        return TextureFormat::RGBA8Srgb;
    for (unsigned i = 0; i < dxgi_formats.size(); ++i)
        if (unsigned(dxgi_formats[i]) == dxgi)
            return TextureFormat(i);
    throw std::runtime_error("DDS pixel format is outside the supported texture profile");
}
// This is admission, not another decoder. The selected native DDS reader has
// private format deduction and forms payload pointers before rejecting truncation.
// Check its admitted layouts first, then compare its actual result with this header.
unsigned legacy_format(Header h, bool& luminance) {
    const auto flags = h.at(80), code = h.at(84), bits = h.at(88);
    auto mask = [&](unsigned r, unsigned g, unsigned b, unsigned a) {
        return h.at(92) == r && h.at(96) == g && h.at(100) == b && h.at(104) == a;
    };
    if (flags & 0x40) {
        if (bits == 32) {
            if (mask(0xff, 0xff00, 0xff0000, 0xff000000))
                return DXGI_FORMAT_R8G8B8A8_UNORM;
            if (mask(0xff0000, 0xff00, 0xff, 0xff000000))
                return DXGI_FORMAT_B8G8R8A8_UNORM;
            if (mask(0xff0000, 0xff00, 0xff, 0))
                return DXGI_FORMAT_B8G8R8X8_UNORM;
            if (mask(0xffff, 0xffff0000, 0, 0))
                return DXGI_FORMAT_R16G16_UNORM;
        }
    } else if (flags & 0x20000) {
        luminance = true;
        if (bits == 8 && mask(0xff, 0, 0, 0))
            return DXGI_FORMAT_R8_UNORM;
        if (bits == 16 && mask(0xffff, 0, 0, 0))
            return DXGI_FORMAT_R16_UNORM;
        if (bits == 16 && mask(0xff, 0, 0, 0xff00))
            return DXGI_FORMAT_R8G8_UNORM;
    } else if (flags & 4) {
        switch (code) {
        case fourcc("DXT1"):
            return DXGI_FORMAT_BC1_UNORM;
        case fourcc("DXT2"):
        case fourcc("DXT3"):
            return DXGI_FORMAT_BC2_UNORM;
        case fourcc("DXT4"):
        case fourcc("DXT5"):
            return DXGI_FORMAT_BC3_UNORM;
        case fourcc("ATI1"):
        case fourcc("BC4U"):
            return DXGI_FORMAT_BC4_UNORM;
        case fourcc("BC4S"):
            return DXGI_FORMAT_BC4_SNORM;
        case fourcc("ATI2"):
        case fourcc("BC5U"):
            return DXGI_FORMAT_BC5_UNORM;
        case fourcc("BC5S"):
            return DXGI_FORMAT_BC5_SNORM;
        case 36:
            return DXGI_FORMAT_R16G16B16A16_UNORM;
        case 111:
            return DXGI_FORMAT_R16_FLOAT;
        case 112:
            return DXGI_FORMAT_R16G16_FLOAT;
        case 113:
            return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case 114:
            return DXGI_FORMAT_R32_FLOAT;
        case 115:
            return DXGI_FORMAT_R32G32_FLOAT;
        case 116:
            return DXGI_FORMAT_R32G32B32A32_FLOAT;
        }
    }
    throw std::runtime_error("Unsupported legacy DDS masks/FourCC");
}
void expand_luminance(TextureData& t, TextureLimits limits) {
    const auto source_format = texture_format_info(t.format);
    const auto source_channels = source_format.channels,
               component = source_format.block_bytes / source_channels;
    auto source_data = std::move(t.subresources);
    auto description = t;
    description.format = component == 2 ? TextureFormat::RGBA16 : TextureFormat::RGBA8;
    validate_texture_metadata(description, limits);
    for (std::size_t i = 0; i < source_data.size(); ++i) {
        const auto mip = unsigned(i % t.mips);
        const auto layout = texture_layout(description, mip), old = texture_layout(t, mip);
        std::vector<std::byte> expanded(layout.bytes);
        Diligent::CopyPixelsAttribs copy;
        copy.Width = layout.width;
        copy.Height = layout.height;
        copy.SrcComponentSize = copy.DstComponentSize = component;
        copy.SrcCompCount = source_channels;
        copy.DstCompCount = 4;
        copy.SrcStride = static_cast<Diligent::Uint32>(old.row_bytes);
        copy.DstStride = static_cast<Diligent::Uint32>(layout.row_bytes);
        copy.Swizzle.R = copy.Swizzle.G = copy.Swizzle.B = Diligent::TEXTURE_COMPONENT_SWIZZLE_R;
        copy.Swizzle.A = source_channels == 2 ? Diligent::TEXTURE_COMPONENT_SWIZZLE_G
                                              : Diligent::TEXTURE_COMPONENT_SWIZZLE_ONE;
        for (unsigned z = 0; z < layout.depth; ++z) {
            copy.pSrcPixels = source_data[i].data() + z * old.slice_bytes;
            copy.pDstPixels = expanded.data() + z * layout.slice_bytes;
            Diligent::CopyPixels(copy);
        }
        description.subresources.push_back(std::move(expanded));
    }
    description.alpha = source_channels == 2 ? TextureAlpha::Straight : TextureAlpha::Opaque;
    validate_texture(description, limits);
    t = std::move(description);
}
} // namespace
TextureData import_texture_dds(std::span<const std::byte> bytes, TextureSemantic semantic,
                               TextureLimits limits, std::stop_token stop) {
    require(!stop.stop_requested(), "DDS import cancelled");
    require(bytes.size() >= 128 && bytes.size() <= limits.bytes, "Invalid or oversized DDS source");
    Header h{bytes};
    require(h.at(0) == fourcc("DDS ") && h.at(4) == 124 && h.at(76) == 32,
            "Invalid DDS signature/header size");
    const auto flags = h.at(8), pf = h.at(80), code = h.at(84), caps = h.at(112);
    TextureData expected;
    expected.width = h.at(16);
    expected.height = h.at(12);
    expected.mips = std::max(1u, h.at(28));
    expected.semantic = semantic;
    bool cube = false, volume = false, luminance = false;
    unsigned dxgi = 0;
    std::size_t offset = 128;
    const bool dx10 = (pf & 4) && code == fourcc("DX10");
    if (dx10) {
        require(bytes.size() >= 148, "Truncated DDS DX10 extension");
        offset = 148;
        dxgi = h.at(128);
        const auto dimension = h.at(132), misc = h.at(136), alpha = h.at(144);
        require(dimension == 3 || dimension == 4, "Only DDS 2D/3D textures are admitted");
        require((misc & ~4u) == 0 && alpha <= 4, "Unsupported DDS DX10 misc/alpha flags");
        expected.layers = h.at(140);
        require(expected.layers, "DDS array size cannot be zero");
        cube = (misc & 4) != 0;
        volume = dimension == 4;
        require(!volume || (!cube && expected.layers == 1 && (flags & 0x800000)),
                "DDS volume/array/cube fields disagree");
        expected.alpha = alpha == 1   ? TextureAlpha::Straight
                         : alpha == 2 ? TextureAlpha::Premultiplied
                         : alpha == 3 ? TextureAlpha::Opaque
                         : alpha == 4 ? TextureAlpha::Custom
                                      : TextureAlpha::Unknown;
    } else {
        dxgi = legacy_format(h, luminance);
        volume = (flags & 0x800000) != 0;
        cube = (caps & 0x200) != 0;
        require(!cube || (caps & 0xfc00) == 0xfc00, "DDS cubemap requires six faces");
        require(!volume || !cube, "DDS volume cannot be a cubemap");
        if ((pf & 4) && (code == fourcc("DXT2") || code == fourcc("DXT4")))
            expected.alpha = TextureAlpha::Premultiplied;
    }
    expected.depth = volume ? h.at(24) : 1;
    require(volume || h.at(24) <= 1, "Non-volume DDS declares depth");
    expected.dimension =
        volume ? TextureDimension::D3
        : cube ? (expected.layers > 1 ? TextureDimension::CubeArray : TextureDimension::Cube)
        : expected.layers > 1 ? TextureDimension::D2Array
                              : TextureDimension::D2;
    expected.format = format_of(dxgi);
    validate_texture_metadata(expected, limits);
    // Match the exact native reader's currently supported dimension profile.
    require(expected.mips <= 15 && expected.layers * (cube ? 6u : 1u) <= 2048 &&
                (!volume || (expected.width <= 2048 && expected.height <= 2048)),
            "DDS dimensions exceed native loader profile");
    std::size_t total = 0;
    for (unsigned mip = 0; mip < expected.mips; ++mip) {
        const auto layout = texture_layout(expected, mip);
        const auto size = layout.bytes * expected.layers * (cube ? 6u : 1u);
        require(size <= limits.bytes - total, "DDS payload exceeds budget");
        total += size;
    }
    require(bytes.size() - offset == total, "DDS pixel payload is truncated or has trailing bytes");
    // MakeCopy requests aligned owned storage before the native typed header reads.
    Diligent::TextureLoadInfo load;
    load.GenerateMips = false;
    load.MipLevels = expected.mips;
    Diligent::RefCntAutoPtr<Diligent::ITextureLoader> loader;
    Diligent::CreateTextureLoaderFromMemory(bytes.data(), bytes.size(), true, load, &loader);
    require(bool(loader), "Native DDS admission failed");
    require(!stop.stop_requested(), "DDS import cancelled");
    auto result = copy_diligent_texture(*loader, semantic, expected.alpha, limits);
    require(result.width == expected.width && result.height == expected.height &&
                result.depth == expected.depth && result.layers == expected.layers &&
                result.mips == expected.mips && result.dimension == expected.dimension &&
                result.format == expected.format,
            "Native DDS metadata disagrees with admission");
    if (luminance && semantic == TextureSemantic::Color)
        expand_luminance(result, limits);
    validate_texture(result, limits);
    return result;
}
} // namespace forge::asset_detail
