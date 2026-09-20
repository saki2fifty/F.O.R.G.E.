#include "texture_diligent.hpp"
#include <TextureUtilities.h>
#include <bit>
#include <cstring>
namespace forge::asset_detail {
static_assert(std::endian::native == std::endian::little,
              "Native texture adapter needs explicit endian conversion on this host");
// Exact native mappings; no Diligent enum values enter cooked artifacts.
namespace {
constexpr std::pair<TextureFormat, Diligent::TEXTURE_FORMAT> formats[]{
    {TextureFormat::R8, Diligent::TEX_FORMAT_R8_UNORM},
    {TextureFormat::RG8, Diligent::TEX_FORMAT_RG8_UNORM},
    {TextureFormat::RGBA8, Diligent::TEX_FORMAT_RGBA8_UNORM},
    {TextureFormat::RGBA8Srgb, Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB},
    {TextureFormat::R16, Diligent::TEX_FORMAT_R16_UNORM},
    {TextureFormat::RG16, Diligent::TEX_FORMAT_RG16_UNORM},
    {TextureFormat::RGBA16, Diligent::TEX_FORMAT_RGBA16_UNORM},
    {TextureFormat::R16Float, Diligent::TEX_FORMAT_R16_FLOAT},
    {TextureFormat::RG16Float, Diligent::TEX_FORMAT_RG16_FLOAT},
    {TextureFormat::RGBA16Float, Diligent::TEX_FORMAT_RGBA16_FLOAT},
    {TextureFormat::R32Float, Diligent::TEX_FORMAT_R32_FLOAT},
    {TextureFormat::RG32Float, Diligent::TEX_FORMAT_RG32_FLOAT},
    {TextureFormat::RGBA32Float, Diligent::TEX_FORMAT_RGBA32_FLOAT},
    {TextureFormat::BC1, Diligent::TEX_FORMAT_BC1_UNORM},
    {TextureFormat::BC1Srgb, Diligent::TEX_FORMAT_BC1_UNORM_SRGB},
    {TextureFormat::BC2, Diligent::TEX_FORMAT_BC2_UNORM},
    {TextureFormat::BC2Srgb, Diligent::TEX_FORMAT_BC2_UNORM_SRGB},
    {TextureFormat::BC3, Diligent::TEX_FORMAT_BC3_UNORM},
    {TextureFormat::BC3Srgb, Diligent::TEX_FORMAT_BC3_UNORM_SRGB},
    {TextureFormat::BC4, Diligent::TEX_FORMAT_BC4_UNORM},
    {TextureFormat::BC4Snorm, Diligent::TEX_FORMAT_BC4_SNORM},
    {TextureFormat::BC5, Diligent::TEX_FORMAT_BC5_UNORM},
    {TextureFormat::BC5Snorm, Diligent::TEX_FORMAT_BC5_SNORM},
    {TextureFormat::BC6Unsigned, Diligent::TEX_FORMAT_BC6H_UF16},
    {TextureFormat::BC6Signed, Diligent::TEX_FORMAT_BC6H_SF16},
    {TextureFormat::BC7, Diligent::TEX_FORMAT_BC7_UNORM},
    {TextureFormat::BC7Srgb, Diligent::TEX_FORMAT_BC7_UNORM_SRGB},
};
}
Diligent::TEXTURE_FORMAT diligent_texture_format(TextureFormat format) {
    for (auto [local, native] : formats)
        if (local == format)
            return native;
    throw std::runtime_error("Unknown FORGE texture format");
}
TextureFormat forge_texture_format(Diligent::TEXTURE_FORMAT format) {
    for (auto [local, native] : formats)
        if (native == format)
            return local;
    throw std::runtime_error("Native texture format is not admitted in this profile");
}
TextureData copy_diligent_texture(Diligent::ITextureLoader& loader, TextureSemantic semantic,
                                  TextureAlpha alpha, TextureLimits limits) {
    const auto& desc = loader.GetTextureDesc();
    TextureData result;
    switch (desc.Type) {
    case Diligent::RESOURCE_DIM_TEX_2D:
        result.dimension = TextureDimension::D2;
        break;
    case Diligent::RESOURCE_DIM_TEX_2D_ARRAY:
        result.dimension = TextureDimension::D2Array;
        break;
    case Diligent::RESOURCE_DIM_TEX_CUBE:
        result.dimension = TextureDimension::Cube;
        break;
    case Diligent::RESOURCE_DIM_TEX_CUBE_ARRAY:
        result.dimension = TextureDimension::CubeArray;
        break;
    case Diligent::RESOURCE_DIM_TEX_3D:
        result.dimension = TextureDimension::D3;
        break;
    default:
        throw std::runtime_error("Unsupported native texture dimension");
    }
    result.width = desc.Width;
    result.height = desc.Height;
    result.mips = desc.MipLevels;
    const bool bgra = desc.Format == Diligent::TEX_FORMAT_BGRA8_UNORM ||
                      desc.Format == Diligent::TEX_FORMAT_BGRA8_UNORM_SRGB ||
                      desc.Format == Diligent::TEX_FORMAT_BGRX8_UNORM ||
                      desc.Format == Diligent::TEX_FORMAT_BGRX8_UNORM_SRGB;
    const bool bgrx = desc.Format == Diligent::TEX_FORMAT_BGRX8_UNORM ||
                      desc.Format == Diligent::TEX_FORMAT_BGRX8_UNORM_SRGB;
    result.format = bgra ? (desc.Format == Diligent::TEX_FORMAT_BGRA8_UNORM_SRGB ||
                                    desc.Format == Diligent::TEX_FORMAT_BGRX8_UNORM_SRGB
                                ? TextureFormat::RGBA8Srgb
                                : TextureFormat::RGBA8)
                         : forge_texture_format(desc.Format);
    result.semantic = semantic;
    result.alpha = alpha;
    if (bgrx)
        result.alpha = TextureAlpha::Opaque;
    const bool volume = result.dimension == TextureDimension::D3;
    const bool cube = result.dimension == TextureDimension::Cube ||
                      result.dimension == TextureDimension::CubeArray;
    result.depth = volume ? desc.Depth : 1;
    const unsigned slices = volume ? 1 : desc.ArraySize;
    if (cube && slices % 6)
        throw std::runtime_error("Native cubemap face count mismatch");
    result.layers = cube ? slices / 6 : slices;
    validate_texture_metadata(result, limits);
    if (!result.width || !result.height || !result.depth || !result.layers || !result.mips ||
        result.mips > 32 || result.width > limits.dimension || result.height > limits.dimension ||
        result.depth > limits.depth || result.layers > limits.layers ||
        slices > limits.subresources / result.mips)
        throw std::runtime_error("Native texture dimensions exceed admission bounds");
    std::size_t total = 0;
    const auto info = texture_format_info(result.format);
    for (unsigned slice = 0; slice < slices; ++slice)
        for (unsigned mip = 0; mip < result.mips; ++mip) {
            const auto layout = texture_layout(result, mip);
            if (layout.bytes > limits.bytes - total)
                throw std::runtime_error("Native texture exceeds byte budget");
            total += layout.bytes;
            const auto& source = loader.GetSubresourceData(mip, slice);
            const auto rows =
                layout.height / info.block_height + (layout.height % info.block_height != 0);
            if (!source.pData || source.pSrcBuffer || source.Stride < layout.row_bytes ||
                source.Stride > SIZE_MAX / rows ||
                (layout.depth > 1 && (source.DepthStride < source.Stride * rows ||
                                      source.DepthStride > SIZE_MAX / layout.depth)))
                throw std::runtime_error("Invalid native texture row/depth stride");
            auto& bytes = result.subresources.emplace_back(layout.bytes);
            if (bgra) {
                if (source.Stride > UINT32_MAX || layout.row_bytes > UINT32_MAX)
                    throw std::runtime_error("Native BGRA stride exceeds pixel utility range");
                Diligent::CopyPixelsAttribs copy;
                copy.Width = layout.width;
                copy.Height = layout.height;
                copy.SrcComponentSize = copy.DstComponentSize = 1;
                copy.SrcCompCount = copy.DstCompCount = 4;
                copy.SrcStride = static_cast<Diligent::Uint32>(source.Stride);
                copy.DstStride = static_cast<Diligent::Uint32>(layout.row_bytes);
                copy.Swizzle.R = Diligent::TEXTURE_COMPONENT_SWIZZLE_B;
                copy.Swizzle.B = Diligent::TEXTURE_COMPONENT_SWIZZLE_R;
                copy.Swizzle.A = bgrx ? Diligent::TEXTURE_COMPONENT_SWIZZLE_ONE
                                      : Diligent::TEXTURE_COMPONENT_SWIZZLE_A;
                for (unsigned z = 0; z < layout.depth; ++z) {
                    copy.pSrcPixels = static_cast<const std::byte*>(source.pData) +
                                      std::size_t(z) * source.DepthStride;
                    copy.pDstPixels = bytes.data() + std::size_t(z) * layout.slice_bytes;
                    Diligent::CopyPixels(copy);
                }
                continue;
            }
            for (unsigned z = 0; z < layout.depth; ++z)
                for (unsigned row = 0; row < rows; ++row)
                    std::memcpy(bytes.data() + std::size_t(z) * layout.slice_bytes +
                                    std::size_t(row) * layout.row_bytes,
                                static_cast<const std::byte*>(source.pData) +
                                    std::size_t(z) * source.DepthStride +
                                    std::size_t(row) * source.Stride,
                                layout.row_bytes);
        }
    validate_texture(result, limits);
    return result;
}
} // namespace forge::asset_detail
