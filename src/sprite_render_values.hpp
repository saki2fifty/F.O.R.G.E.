#pragma once
#include "render_values.hpp"
#include "sprite_draw_values.hpp"
#include "sprite_material_profile.hpp"
#include <forge/transform.hpp>

namespace forge {
inline SpriteDrawValues sprite_draw_values(const SpriteRenderer& sprite,
                                           std::uint32_t texture_width,
                                           std::uint32_t texture_height) {
    detail::validate_sprite_renderer(sprite);
    const SpriteGridLayout grid{sprite.columns,  sprite.rows,      sprite.margin_x,
                                sprite.margin_y, sprite.spacing_x, sprite.spacing_y};
    const auto crop =
        sprite.region_enabled
            ? std::optional<SpritePixelRegion>{{sprite.region_x, sprite.region_y,
                                                sprite.region_width, sprite.region_height}}
            : std::nullopt;
    const auto region = sprite_grid_region(texture_width, texture_height, grid, sprite.frame, crop);
    return {
        sprite_region_mapping(region, texture_width, texture_height, sprite.flip_x, sprite.flip_y),
        {sprite.tint_r, sprite.tint_g, sprite.tint_b, sprite.tint_a},
        {sprite.image_mode,
         {sprite_image_axis(sprite.image_mode, region.width, sprite.border_left,
                            sprite.border_right, sprite.width, sprite.pixels_per_meter),
          sprite_image_axis(sprite.image_mode, region.height, sprite.border_top,
                            sprite.border_bottom, sprite.height, sprite.pixels_per_meter)}}};
}
inline SpriteDrawValues sprite_draw_values(const SpriteRenderer& sprite,
                                           const TextureData& source) {
    validate_sprite_texture_profile(source);
    return sprite_draw_values(sprite, source.width, source.height);
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
