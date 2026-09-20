#include "texture_import.hpp"
#include "texture_decode.h"
#include "texture_diligent.hpp"
#include <DataBlobImpl.hpp>
#include <Image.h>
#include <RefCntAutoPtr.hpp>
#include <TextureUtilities.h>
#include <bit>
#include <cmath>
#include <cstring>
namespace forge::asset_detail {
namespace {
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
void cancelled(std::stop_token stop) {
    require(!stop.stop_requested(), "Texture import cancelled");
}
void validate_tga_extent(std::span<const std::byte> bytes, unsigned dimension_limit) {
    require(bytes.size() >= 18, "Truncated TGA header");
    auto u8 = [&](std::size_t at) { return std::to_integer<unsigned>(bytes[at]); };
    auto u16 = [&](std::size_t at) { return u8(at) | (u8(at + 1) << 8); };
    const auto type = u8(2), depth = u8(16), map = u8(1);
    const bool indexed = type == 1 || type == 9;
    const bool gray = type == 3 || type == 11;
    require(type == 1 || type == 2 || type == 3 || type == 9 || type == 10 || type == 11,
            "Unsupported TGA image type");
    require(map <= 1 && (!indexed || map == 1), "Invalid TGA color map");
    require(indexed ? depth == 8 || depth == 16
            : gray  ? depth == 8 || depth == 16
                    : depth == 15 || depth == 16 || depth == 24 || depth == 32,
            "Unsupported TGA pixel depth");
    const auto map_depth = u8(7), entries = u16(5), origin = u16(3);
    if (map)
        require(entries &&
                    (map_depth == 15 || map_depth == 16 || map_depth == 24 || map_depth == 32),
                "Unsupported TGA palette");
    std::size_t at = 18;
    auto consume = [&](std::size_t n) {
        require(n <= bytes.size() - at, "Truncated TGA pixels/palette");
        const auto start = at;
        at += n;
        return start;
    };
    consume(u8(0));
    if (map)
        consume(std::size_t(entries) * ((map_depth + 7) / 8));
    require(u16(12) <= dimension_limit && u16(14) <= dimension_limit,
            "TGA dimensions exceed bounds");
    std::size_t remaining = std::size_t(u16(12)) * u16(14);
    require(remaining != 0, "Empty TGA dimensions");
    const auto pixel_bytes = (depth + 7) / 8;
    while (remaining) {
        unsigned count = 1;
        bool repeated = false;
        if (type >= 9) {
            const auto packet = u8(consume(1));
            count = (packet & 127) + 1;
            repeated = (packet & 128) != 0;
        } else
            count = static_cast<unsigned>(std::min<std::size_t>(remaining, 65536));
        require(count <= remaining, "TGA packet exceeds pixel extent");
        const auto stored = repeated ? 1 : count;
        const auto start = consume(std::size_t(stored) * pixel_bytes);
        if (indexed)
            for (unsigned i = 0; i < stored; ++i) {
                const auto index = pixel_bytes == 1 ? u8(start + i) : u16(start + 2 * i);
                require(index >= origin && index - origin < entries,
                        "TGA palette index out of range");
            }
        remaining -= count;
    }
    // TGA extension/developer areas and the standard footer may follow pixels.
}
void normals(TextureData& t, bool flip) {
    const auto f = texture_format_info(t.format);
    require(!f.compressed && f.channels >= 2 && !f.srgb, "Normal map needs linear pixels");
    const auto size = f.block_bytes / f.channels;
    require(size == 1 || size == 2 || f.float_bits == 32, "Unsupported normal scalar");
    auto read = [&](const std::byte* p) {
        if (f.float_bits == 32) {
            std::uint32_t v = 0;
            for (unsigned k = 0; k < 4; ++k)
                v |= std::to_integer<std::uint32_t>(p[k]) << (8 * k);
            return double(std::bit_cast<float>(v)) * 2 - 1;
        }
        require(!f.float_bits, "Half-float normal conversion needs an explicit codec");
        const auto v = std::to_integer<unsigned>(p[0]) +
                       (size == 2 ? (std::to_integer<unsigned>(p[1]) << 8) : 0);
        return double(v) / (size == 1 ? 255 : 65535) * 2 - 1;
    };
    auto write = [&](std::byte* p, double x) {
        x = std::clamp(x * .5 + .5, 0., 1.);
        std::uint32_t v = f.float_bits == 32
                              ? std::bit_cast<std::uint32_t>(float(x))
                              : std::uint32_t(std::lround(x * (size == 1 ? 255 : 65535)));
        for (unsigned k = 0; k < size; ++k)
            p[k] = std::byte((v >> (8 * k)) & 255);
    };
    for (auto& bytes : t.subresources)
        for (std::size_t at = 0; at < bytes.size(); at += f.block_bytes) {
            double x = read(bytes.data() + at), y = read(bytes.data() + at + size),
                   z = f.channels >= 3 ? read(bytes.data() + at + 2 * size)
                                       : std::sqrt(std::max(0., 1 - x * x - y * y));
            if (flip)
                y = -y;
            const auto length = std::hypot(x, y, z);
            if (length < 1e-12) {
                x = 0;
                y = 0;
                z = 1;
            } else {
                x /= length;
                y /= length;
                z /= length;
            }
            write(bytes.data() + at, x);
            write(bytes.data() + at + size, y);
            if (f.channels >= 3)
                write(bytes.data() + at + 2 * size, z);
        }
}
TextureAlpha alpha_of(const TextureData& t, bool premultiply) {
    const auto f = texture_format_info(t.format);
    if (f.channels < 4)
        return TextureAlpha::Opaque;
    const auto scalar = f.block_bytes / f.channels;
    const auto& base = t.subresources.front();
    for (std::size_t at = 3 * scalar; at < base.size(); at += f.block_bytes) {
        bool opaque = false;
        if (f.float_bits == 32) {
            std::uint32_t v = 0;
            for (unsigned k = 0; k < 4; ++k)
                v |= std::to_integer<unsigned>(base[at + k]) << (8 * k);
            opaque = std::bit_cast<float>(v) == 1;
        } else if (!f.float_bits) {
            opaque = true;
            for (unsigned k = 0; k < scalar; ++k)
                opaque = opaque && base[at + k] == std::byte{255};
        }
        if (!opaque)
            return premultiply ? TextureAlpha::Premultiplied : TextureAlpha::Straight;
    }
    return TextureAlpha::Opaque;
}
} // namespace
TextureData import_texture_image(std::span<const std::byte> bytes, std::string_view name_hint,
                                 const TextureImportSettings& settings, TextureLimits limits,
                                 std::stop_token stop) {
    cancelled(stop);
    require(!bytes.empty() && bytes.size() <= limits.bytes && bytes.size() <= INT32_MAX,
            "Image source exceeds bounds");
    require(name_hint.size() <= 1024 && name_hint.find('\0') == std::string_view::npos,
            "Invalid image name hint");
    require(settings.max_size && settings.max_size <= limits.dimension &&
                unsigned(settings.compression) <= 2,
            "Invalid image import settings");
    require(!settings.srgb || settings.semantic == TextureSemantic::Color,
            "Data/normal/HDR import cannot be sRGB");
    require(!settings.flip_normal_green || settings.semantic == TextureSemantic::Normal,
            "Green flip requires normal semantic");
    require(!settings.premultiply_alpha || settings.semantic == TextureSemantic::Color ||
                settings.semantic == TextureSemantic::HdrColor,
            "Premultiplication requires color semantic");
    validate_sampler(settings.sampler);
    const std::string hint(name_hint);
    const bool webp = bytes.size() >= 12 && std::memcmp(bytes.data(), "RIFF", 4) == 0 &&
                      std::memcmp(bytes.data() + 8, "WEBP", 4) == 0;
    const bool bmp = bytes.size() >= 2 && bytes[0] == std::byte{'B'} && bytes[1] == std::byte{'M'};
    const auto format =
        (bmp || webp)
            ? Diligent::IMAGE_FILE_FORMAT_UNKNOWN
            : Diligent::Image::GetFileFormat(reinterpret_cast<const Diligent::Uint8*>(bytes.data()),
                                             bytes.size(), hint.c_str());
    require(bmp || webp || format == Diligent::IMAGE_FILE_FORMAT_PNG ||
                format == Diligent::IMAGE_FILE_FORMAT_JPEG ||
                format == Diligent::IMAGE_FILE_FORMAT_TGA ||
                format == Diligent::IMAGE_FILE_FORMAT_HDR,
            "Source format needs a supported image decoder or the container importer");
    if (format == Diligent::IMAGE_FILE_FORMAT_TGA)
        validate_tga_extent(bytes, limits.dimension);
    if (bmp)
        validate_bmp_extent(bytes, limits.dimension);
    if (format == Diligent::IMAGE_FILE_FORMAT_HDR)
        validate_hdr_extent(bytes, limits.dimension);
    Diligent::ImageDesc desc;
    const auto bounded_decoder = webp                                         ? forge_webp_decode
                                 : bmp                                        ? forge_bmp_decode
                                 : format == Diligent::IMAGE_FILE_FORMAT_PNG  ? forge_png_decode
                                 : format == Diligent::IMAGE_FILE_FORMAT_JPEG ? forge_jpeg_decode
                                                                              : nullptr;
    if (bounded_decoder) {
        ForgeImagePixels header{};
        require(
            bounded_decoder(bytes.data(), bytes.size(), limits.dimension, limits.bytes, 1, &header),
            header.error);
        desc.Width = header.width;
        desc.Height = header.height;
        desc.NumComponents = header.channels;
        desc.ComponentType = header.component_bytes == 2 ? Diligent::VT_UINT16 : Diligent::VT_UINT8;
    } else
        desc = Diligent::Image::GetDesc(format, bytes.data(), bytes.size());
    require(desc.Width && desc.Height && desc.NumComponents && desc.NumComponents <= 4 &&
                desc.Width <= limits.dimension && desc.Height <= limits.dimension,
            "Decoded image dimensions exceed bounds");
    const bool hdr = desc.ComponentType == Diligent::VT_FLOAT32;
    require(!settings.srgb || desc.ComponentType == Diligent::VT_UINT8,
            "sRGB images need 8-bit normalized source; use explicit linear storage for higher "
            "precision");
    require(!hdr || settings.compression == TextureCompression::None,
            "Native BC1/3/4/5 encoder does not encode HDR pixels");
    Diligent::TextureLoadInfo load;
    load.IsSRGB = settings.srgb;
    load.GenerateMips =
        settings.generate_mips || desc.Width > settings.max_size || desc.Height > settings.max_size;
    load.MipLevels = load.GenerateMips ? 0 : 1;
    load.FlipVertically = settings.flip_vertical;
    load.PermultiplyAlpha = settings.premultiply_alpha;
    // Bound the actual normalized/floating output channel layout before decode.
    const unsigned component_bytes = desc.ComponentType == Diligent::VT_UINT8    ? 1
                                     : desc.ComponentType == Diligent::VT_UINT16 ? 2
                                     : hdr                                       ? 4
                                                                                 : 0;
    require(component_bytes != 0, "Unsupported native image scalar type");
    const bool color = settings.semantic == TextureSemantic::Color ||
                       settings.semantic == TextureSemantic::HdrColor;
    const unsigned output_channels =
        desc.NumComponents == 3 || settings.srgb || color ? 4 : desc.NumComponents;
    std::uint64_t prepared_bytes = 0;
    unsigned width = desc.Width, height = desc.Height;
    for (;;) {
        const auto row_bytes = std::uint64_t(width) * output_channels * component_bytes;
        require(row_bytes <= UINT32_MAX - 3 && row_bytes <= limits.bytes / height,
                "Decoded image row/pixels exceed native bounds");
        const auto level_bytes = row_bytes * height;
        require(level_bytes <= limits.bytes - prepared_bytes,
                "Decoded image/mips exceed worker pixel budget");
        prepared_bytes += level_bytes;
        if (!load.GenerateMips || (width == 1 && height == 1))
            break;
        width = std::max(1u, width / 2);
        height = std::max(1u, height / 2);
    }
    Diligent::RefCntAutoPtr<Diligent::Image> image;
    Diligent::ImageLoadInfo image_info;
    image_info.Format = format;
    image_info.PermultiplyAlpha = false;
    image_info.IsSRGB = settings.srgb;
    if (bounded_decoder) {
        ForgeImagePixels decoded{};
        require(bounded_decoder(bytes.data(), bytes.size(), limits.dimension, limits.bytes, 0,
                                &decoded),
                decoded.error);
        struct Cleanup {
            ForgeImagePixels* value;
            ~Cleanup() { forge_image_pixels_free(value); }
        } cleanup{&decoded};
        require(decoded.width == desc.Width && decoded.height == desc.Height &&
                    decoded.channels == desc.NumComponents,
                "Image decode/header mismatch");
        desc.RowStride = static_cast<Diligent::Uint32>(decoded.stride);
        auto data = Diligent::DataBlobImpl::Create(decoded.size, decoded.pixels);
        Diligent::Image::CreateFromPixels(desc, std::move(data), &image);
    } else
        Diligent::Image::CreateFromMemory(bytes.data(), bytes.size(), image_info, &image);
    require(bool(image), "Native image decode failed");
    if (color && image->GetDesc().NumComponents < 3) {
        const auto source = image->GetDesc();
        auto expanded = source;
        expanded.NumComponents = 4;
        expanded.RowStride = source.Width * 4 * component_bytes;
        auto pixels =
            Diligent::DataBlobImpl::Create(std::size_t(expanded.RowStride) * expanded.Height);
        Diligent::CopyPixelsAttribs copy;
        copy.Width = source.Width;
        copy.Height = source.Height;
        copy.SrcComponentSize = component_bytes;
        copy.DstComponentSize = component_bytes;
        copy.SrcCompCount = source.NumComponents;
        copy.DstCompCount = 4;
        copy.SrcStride = source.RowStride;
        copy.DstStride = expanded.RowStride;
        copy.pSrcPixels = image->GetData()->GetConstDataPtr();
        copy.pDstPixels = pixels->GetDataPtr();
        copy.Swizzle.R = Diligent::TEXTURE_COMPONENT_SWIZZLE_R;
        copy.Swizzle.G = Diligent::TEXTURE_COMPONENT_SWIZZLE_R;
        copy.Swizzle.B = Diligent::TEXTURE_COMPONENT_SWIZZLE_R;
        copy.Swizzle.A = source.NumComponents == 2 ? Diligent::TEXTURE_COMPONENT_SWIZZLE_G
                                                   : Diligent::TEXTURE_COMPONENT_SWIZZLE_ONE;
        Diligent::CopyPixels(copy);
        image.Release();
        Diligent::Image::CreateFromPixels(expanded, std::move(pixels), &image);
        require(bool(image), "Color channel expansion failed");
    }
    if (settings.premultiply_alpha && image->GetDesc().NumComponents == 4) {
        const auto& pixels = image->GetDesc();
        Diligent::PremultiplyAlphaAttribs attribs;
        attribs.Width = pixels.Width;
        attribs.Height = pixels.Height;
        attribs.ComponentType = pixels.ComponentType;
        attribs.ComponentCount = 4;
        attribs.Stride = pixels.RowStride;
        attribs.pPixels = image->GetData()->GetDataPtr();
        attribs.IsSRGB = settings.srgb;
        Diligent::PremultiplyAlpha(attribs);
    }
    cancelled(stop);
    Diligent::RefCntAutoPtr<Diligent::ITextureLoader> loader;
    Diligent::CreateTextureLoaderFromImage(image, load, &loader);
    require(bool(loader), "Native texture processing failed");
    cancelled(stop);
    auto result = copy_diligent_texture(*loader, settings.semantic, TextureAlpha::Unknown, limits);
    loader.Release();
    image.Release();
    // Native byte-copy expansion fills float alpha with zero; RGBE has no alpha.
    // Preserve opaque source semantics explicitly, using real IEEE float one.
    if (hdr && desc.NumComponents < 4 && result.format == TextureFormat::RGBA32Float)
        for (auto& mip : result.subresources)
            for (std::size_t at = 12; at < mip.size(); at += 16) {
                mip[at] = std::byte{0};
                mip[at + 1] = std::byte{0};
                mip[at + 2] = std::byte{128};
                mip[at + 3] = std::byte{63};
            }
    if (settings.semantic == TextureSemantic::Normal)
        normals(result, settings.flip_normal_green);
    unsigned drop = 0;
    while ((result.width >> drop) > settings.max_size ||
           (result.height >> drop) > settings.max_size)
        ++drop;
    if (drop) {
        result.width = std::max(1u, result.width >> drop);
        result.height = std::max(1u, result.height >> drop);
        result.mips -= drop;
        result.subresources.erase(result.subresources.begin(), result.subresources.begin() + drop);
    }
    if (!settings.generate_mips) {
        result.mips = 1;
        result.subresources.resize(1);
    }
    result.alpha = alpha_of(result, settings.premultiply_alpha);
    result.sampler = settings.sampler;
    if (settings.compression != TextureCompression::None) {
        const auto f = texture_format_info(result.format);
        require(!f.float_bits && f.block_bytes == f.channels,
                "Native BC encoding requires 8-bit normalized pixels");
        TextureFormat compressed = result.format;
        for (unsigned mip = 0; mip < result.mips; ++mip) {
            cancelled(stop);
            const auto layout = texture_layout(result, mip);
            Diligent::TextureDesc source;
            source.Type = Diligent::RESOURCE_DIM_TEX_2D;
            source.Width = layout.width;
            source.Height = layout.height;
            source.Format = diligent_texture_format(result.format);
            source.MipLevels = 1;
            Diligent::TextureSubResData sub;
            sub.pData = result.subresources[mip].data();
            sub.Stride = layout.row_bytes;
            sub.DepthStride = layout.slice_bytes;
            Diligent::TextureData data;
            data.pSubResources = &sub;
            data.NumSubresources = 1;
            Diligent::TextureLoadInfo compress;
            compress.GenerateMips = false;
            compress.MipLevels = 1;
            compress.IsSRGB = settings.srgb;
            compress.CompressMode = settings.compression == TextureCompression::NativeBc
                                        ? Diligent::TEXTURE_LOAD_COMPRESS_MODE_BC
                                        : Diligent::TEXTURE_LOAD_COMPRESS_MODE_BC_HIGH_QUAL;
            Diligent::CreateTextureLoaderFromTextureData(source, data, false, &compress, &loader);
            require(bool(loader), "Native BC compression failed");
            auto encoded = copy_diligent_texture(*loader, settings.semantic, result.alpha, limits);
            compressed = encoded.format;
            result.subresources[mip] = std::move(encoded.subresources[0]);
            loader.Release();
        }
        result.format = compressed;
    }
    validate_texture(result, limits);
    cancelled(stop);
    return result;
}
} // namespace forge::asset_detail
