#pragma once
#include "material_shader.hpp"
#include <array>
namespace forge {
inline constexpr unsigned decal_color_channel = 0, decal_normal_channel = 1,
                          decal_roughness_channel = 2, decal_channel_count = 3;
inline constexpr unsigned decal_all_channels = (1u << decal_channel_count) - 1;
struct DecalTexture {
    AssetRef<TextureAsset> asset;
    MaterialTextureSlot slot;
    std::array<std::array<float, 4>, 2> uv{{{1, 0, 0, 0}, {0, 1, 0, 0}}};
};
// Immutable projection values derived from an ordinary built-in Material.
// Texture/GPU leases and admission remain with the current resource owners.
struct DecalMaterial {
    std::array<float, 4> color{1, 1, 1, 1};
    MaterialAlpha alpha = MaterialAlpha::Opaque;
    float cutoff = .5f;
    std::array<DecalTexture, decal_channel_count> textures;
    float normal_scale = 1, roughness_scale = 1, roughness_bias = 0;
    unsigned roughness_component = 1;
};
DecalMaterial prepare_decal_material(const MaterialData&, const MaterialTextureBindings&);
} // namespace forge
