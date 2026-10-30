#pragma once
#include "material_shader.hpp"
namespace forge {
// Color projection consumes one admitted built-in Material snapshot. Its normal,
// optical and render-state intent stays with ordinary mesh rendering. Resource
// callers reject custom Surface Shader snapshots before preparing this subset.
struct DecalMaterial {
    std::array<float, 4> color{1, 1, 1, 1};
    MaterialAlpha alpha = MaterialAlpha::Opaque;
    float cutoff = .5f;
    AssetRef<TextureAsset> texture;
    MaterialTextureSlot slot;
    std::array<std::array<float, 4>, 2> uv{{{1, 0, 0, 0}, {0, 1, 0, 0}}};
};
DecalMaterial prepare_decal_material(const MaterialData&, const MaterialTextureBindings&);
} // namespace forge
