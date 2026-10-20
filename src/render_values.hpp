#pragma once
#include <forge/render_components.hpp>
#include <nlohmann/json.hpp>
namespace forge::detail {
using Json = nlohmann::json;
Json render_value(const Camera&);
Json render_value(const Light&);
Json render_value(const ParticleEmitter&);
Json render_value(const SpriteRenderer&);
Json render_value(const TileMapRenderer&);
Camera camera_value(const Json&);
Light light_value(const Json&);
ParticleEmitter particle_emitter_value(const Json&);
SpriteRenderer sprite_renderer_value(const Json&);
TileMapRenderer tile_map_renderer_value(const Json&);
void validate_sprite_renderer(const SpriteRenderer&);
void validate_tile_map_renderer(const TileMapRenderer&);
} // namespace forge::detail
