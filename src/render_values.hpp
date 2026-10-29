#pragma once
#include <forge/reflection_probe.hpp>
#include <forge/render_components.hpp>
#include <nlohmann/json.hpp>
namespace forge::detail {
using Json = nlohmann::json;
Json render_value(const Camera&);
Json render_value(const Light&);
Json render_value(const ReflectionProbe&);
Json render_value(const ParticleEmitter&);
Json render_value(const SpriteRenderer&);
Json render_value(const TileMapRenderer&);
Camera camera_value(const Json&);
Light light_value(const Json&);
ReflectionProbe reflection_probe_value(const Json&);
void validate_reflection_probe(const ReflectionProbe&);
ParticleEmitter particle_emitter_value(const Json&);
SpriteRenderer sprite_renderer_value(const Json&);
std::vector<SpriteFrameRegion> sprite_frame_regions_value(const Json&);
inline std::uint64_t sprite_frame_count(const SpriteRenderer& value) noexcept {
    return value.frame_regions.empty() ? std::uint64_t(value.columns) * value.rows
                                       : value.frame_regions.size();
}
TileMapRenderer tile_map_renderer_value(const Json&);
void validate_sprite_renderer(const SpriteRenderer&);
void validate_tile_map_renderer(const TileMapRenderer&);
} // namespace forge::detail
