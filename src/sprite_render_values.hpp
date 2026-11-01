#pragma once
#include "render_values.hpp"
#include "sprite_draw_values.hpp"
#include "sprite_frame_selection.hpp"
#include "sprite_material_profile.hpp"
#include <forge/transform.hpp>

namespace forge {
// Private filtered frame images already contain the full logical canvas. Keep
// layout, fill and flips while removing physical atlas storage from draw values.
inline SpriteDrawValues sprite_canvas_draw_values(const SpriteRenderer& sprite, std::uint32_t width,
                                                  std::uint32_t height) {
    detail::validate_sprite_renderer(sprite);
    SpriteDrawValues result{
        sprite_region_mapping({0, 0, width, height}, width, height, sprite.flip_x, sprite.flip_y),
        {sprite.tint_r, sprite.tint_g, sprite.tint_b, sprite.tint_a},
        {sprite.image_mode,
         {sprite_image_axis(sprite.image_mode, width, sprite.border_left, sprite.border_right,
                            sprite.width, sprite.pixels_per_meter),
          sprite_image_axis(sprite.image_mode, height, sprite.border_top, sprite.border_bottom,
                            sprite.height, sprite.pixels_per_meter)}}};
    validate_sprite_draw_values(result);
    return result;
}
inline SpriteDrawValues sprite_region_draw_values(const SpriteRenderer& sprite,
                                                  std::uint32_t texture_width,
                                                  std::uint32_t texture_height,
                                                  SpritePixelRegion region,
                                                  const SpriteFrameRegion* frame) {
    const bool rotated = frame && frame->rotated_clockwise;
    const auto canvas =
        frame ? sprite_frame_canvas(*frame)
              : SpriteFrameCanvas{region.width, region.height, region.width, region.height, 0, 0};
    SpriteDrawValues result{
        sprite_region_mapping(region, texture_width, texture_height,
                              rotated ? !sprite.flip_y : sprite.flip_x,
                              rotated ? sprite.flip_x : sprite.flip_y),
        {sprite.tint_r, sprite.tint_g, sprite.tint_b, sprite.tint_a},
        {sprite.image_mode,
         {sprite_image_axis(sprite.image_mode, canvas.width, sprite.border_left,
                            sprite.border_right, sprite.width, sprite.pixels_per_meter),
          sprite_image_axis(sprite.image_mode, canvas.height, sprite.border_top,
                            sprite.border_bottom, sprite.height, sprite.pixels_per_meter)}}};
    result.transpose_uv = rotated;
    result.quad_crop = canvas.quad_region(sprite.flip_x, sprite.flip_y);
    validate_sprite_draw_values(result);
    return result;
}
inline SpriteDrawValues sprite_draw_values(const SpriteRenderer& sprite,
                                           std::uint32_t texture_width,
                                           std::uint32_t texture_height) {
    detail::validate_sprite_renderer(sprite);
    if (sprite.texture_frames)
        throw std::invalid_argument("Texture frame drawing requires its selected Texture metadata");
    const SpriteGridLayout grid{sprite.columns,  sprite.rows,      sprite.margin_x,
                                sprite.margin_y, sprite.spacing_x, sprite.spacing_y};
    const auto crop =
        sprite.region_enabled
            ? std::optional<SpritePixelRegion>{{sprite.region_x, sprite.region_y,
                                                sprite.region_width, sprite.region_height}}
            : std::nullopt;
    const auto region = sprite_frame_region(texture_width, texture_height, grid, sprite.frame, crop,
                                            sprite.frame_regions);
    const auto* frame =
        sprite.frame_regions.empty() ? nullptr : &sprite.frame_regions.at(sprite.frame);
    return sprite_region_draw_values(sprite, texture_width, texture_height, region, frame);
}
inline SpriteDrawValues sprite_draw_values(const SpriteRenderer& sprite,
                                           const TextureData& source) {
    validate_sprite_texture_profile(source);
    if (!sprite.texture_frames)
        return sprite_draw_values(sprite, source.width, source.height);
    detail::validate_sprite_renderer(sprite);
    for (const auto& entry : source.sprite_frames)
        detail::validate_sprite_frame_shape(sprite, entry.region);
    const auto& frame = source.sprite_frames.at(sprite_texture_frame_slot(sprite, source)).region;
    const auto region =
        sprite_frame_region(source.width, source.height, {}, 0, {}, std::span(&frame, 1));
    return sprite_region_draw_values(sprite, source.width, source.height, region, &frame);
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
