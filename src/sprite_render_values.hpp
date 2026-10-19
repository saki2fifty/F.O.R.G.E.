#pragma once
#include "render_values.hpp"
#include "sprite_draw_values.hpp"
#include "sprite_material_profile.hpp"
#include <forge/transform.hpp>

namespace forge {
inline SpriteDrawValues sprite_draw_values(const SpriteRenderer& sprite,
                                           const TextureData& source) {
    detail::validate_sprite_renderer(sprite);
    validate_sprite_texture_profile(source);
    const SpriteGridLayout grid{sprite.columns,  sprite.rows,      sprite.margin_x,
                                sprite.margin_y, sprite.spacing_x, sprite.spacing_y};
    return {
        sprite_region_mapping(sprite_grid_region(source.width, source.height, grid, sprite.frame),
                              source.width, source.height, sprite.flip_x, sprite.flip_y),
        {sprite.tint_r, sprite.tint_g, sprite.tint_b, sprite.tint_a}};
}
inline AffineTransform sprite_world_transform(const SpriteRenderer& sprite,
                                              const AffineTransform& world) {
    detail::validate_sprite_renderer(sprite);
    AffineTransform quad;
    quad.m[0] = sprite.width;
    quad.m[5] = sprite.height;
    quad.m[3] = (.5 - sprite.pivot_x) * sprite.width;
    quad.m[7] = (sprite.pivot_y - .5) * sprite.height;
    return world * quad;
}
} // namespace forge
