#pragma once
#include <forge/texture_asset.hpp>
#include <stop_token>
namespace forge::asset_detail {
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
} // namespace forge::asset_detail
