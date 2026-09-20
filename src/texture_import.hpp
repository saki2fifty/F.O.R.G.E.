#pragma once
#include <forge/texture_asset.hpp>
#include <stop_token>
namespace forge::asset_detail {
void validate_bmp_extent(std::span<const std::byte> bytes, unsigned dimension_limit);
void validate_hdr_extent(std::span<const std::byte> bytes, unsigned dimension_limit);
enum class TextureCompression { None, NativeBc, NativeBcHighQuality };
struct TextureImportSettings {
    TextureSemantic semantic = TextureSemantic::Color;
    bool srgb = true, generate_mips = true, flip_vertical = false;
    bool flip_normal_green = false, premultiply_alpha = false;
    unsigned max_size = 16384;
    TextureCompression compression = TextureCompression::None;
    SamplerState sampler;
};
// Private worker-only decoder. No path opening, world/device access or publication.
TextureData import_texture_image(std::span<const std::byte> bytes, std::string_view name_hint,
                                 const TextureImportSettings& settings, TextureLimits limits = {},
                                 std::stop_token stop = {});
// Container metadata and supplied mips are retained. DX10 transfer/alpha metadata
// is authoritative; legacy DDS does not declare sRGB.
TextureData import_texture_dds(std::span<const std::byte> bytes, TextureSemantic semantic,
                               TextureLimits limits = {}, std::stop_token stop = {});
} // namespace forge::asset_detail
