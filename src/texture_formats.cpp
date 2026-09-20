#include "texture_formats.hpp"
#include <stdexcept>
namespace forge::asset_detail {
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
} // namespace forge::asset_detail
