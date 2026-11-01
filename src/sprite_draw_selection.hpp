#pragma once
#include "model_draw_candidate.hpp"
#include "sprite_material_profile.hpp"
#include <forge/engine_assets.hpp>
#include <forge/render_components.hpp>

namespace forge::asset_detail {
// Validate one complete immutable selection before native allocation. Its texture
// extent is also the authority for per-object atlas rectangles; no second source
// table, resource key or texture lifetime is introduced.
inline const ResourceLease<TextureAsset>& sprite_source_texture_lease(const PreparedModelDraw& draw,
                                                                      bool skinned = false) {
    const auto& mesh = draw.mesh.get().mesh;
    if (skinned || draw.mesh.identity().asset != engine_primitive(8).id || draw.has_fallbacks() ||
        mesh.lods.size() != 1 || mesh.lods[0].parts.size() != 1 ||
        draw.selection.parts.size() != 1 || draw.selection.parts[0].size() != 1 ||
        !draw.selection.unresolved.empty())
        throw std::invalid_argument("Sprite requires the built-in Quad and a complete material "
                                    "selection without skinning or substituted resources");
    const auto material_id = draw.selection.parts[0][0].id;
    const auto& material = draw.materials.at(material_id);
    if (material.identity().asset != material_id)
        throw std::invalid_argument("Sprite requires a matching material resource");
    const auto image = sprite_material_image(material.get());
    const auto source = material->textures.at(std::string(image));
    const auto& texture = draw.textures.at({source.id, TextureSemantic::Color});
    if (texture.identity().asset != source.id)
        throw std::invalid_argument("Sprite texture lease identity does not match its material");
    validate_sprite_texture_profile(texture.get());
    return texture;
}
inline const TextureData& sprite_source_texture(const PreparedModelDraw& draw,
                                                bool skinned = false) {
    return sprite_source_texture_lease(draw, skinned).get();
}
inline void validate_sprite_frame_selection(const PreparedModelDraw& draw,
                                            const SpriteRenderer& sprite) {
    const auto material_id = draw.selection.parts.at(0).at(0).id;
    const auto& material = draw.materials.at(material_id)->values;
    if (sprite.texture_frames) {
        for (const auto& entry : sprite_source_texture(draw).sprite_frames)
            validate_sprite_frame_material(material, std::span(&entry.region, 1));
    } else
        validate_sprite_frame_material(material, sprite.frame_regions);
}
} // namespace forge::asset_detail
