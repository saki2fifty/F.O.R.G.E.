#pragma once
#include <forge/material_asset.hpp>
#include <stdexcept>

namespace forge {
// Material-owned surface intent for the initial base-level Sprite profile.
// Atlas rectangles and modulation remain detached per-object numeric values.
inline void validate_sprite_material_profile(const MaterialData& material) {
    if (material.model != "forge.gltf.unlit.v1" || material.textures.size() != 1 ||
        !material.textures.contains("baseColorTexture"))
        throw std::invalid_argument(
            "Sprite requires an unlit material with one base-color texture");
    const auto& slot = material.textures.at("baseColorTexture");
    if (slot.semantic != TextureSemantic::Color || slot.dimension != TextureDimension::D2 ||
        slot.uv_set != 0 || slot.offset != std::array<float, 2>{0, 0} ||
        slot.scale != std::array<float, 2>{1, 1} || slot.rotation != 0 ||
        slot.sampler.min != slot.sampler.mag || slot.sampler.anisotropy != 1 ||
        slot.sampler.lod_bias != 0 || slot.sampler.min_lod != 0 || !(slot.sampler.max_lod >= 0) ||
        slot.sampler.compare != TextureCompare::None)
        throw std::invalid_argument("Sprite material requires color UV0 with identity transform, "
                                    "matching min/mag filters, non-anisotropic sampling and "
                                    "unshifted base-level LOD");
}
// Resource-pool admission already validates complete payloads. Check the narrower
// Sprite interpretation before native allocation: compression can mix neighboring
// cells inside a block, and premultiplied pixels require different blend math.
inline void validate_sprite_texture_profile(const TextureData& texture) {
    if (!texture.width || !texture.height || texture.dimension != TextureDimension::D2 ||
        texture.depth != 1 || texture.layers != 1 || texture.semantic != TextureSemantic::Color ||
        (texture.format != TextureFormat::RGBA8 && texture.format != TextureFormat::RGBA8Srgb) ||
        (texture.alpha != TextureAlpha::Opaque && texture.alpha != TextureAlpha::Straight))
        throw std::invalid_argument("Sprite requires a 2D RGBA8 color texture with opaque or "
                                    "straight alpha; compressed, HDR and premultiplied sources "
                                    "are not supported by this profile");
}
} // namespace forge
