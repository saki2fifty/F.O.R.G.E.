#pragma once
#include <forge/render_components.hpp>
#include <nlohmann/json.hpp>
namespace forge::detail {
using Json = nlohmann::json;
Json render_value(const Camera&);
Json render_value(const Light&);
Json render_value(const ParticleEmitter&);
Camera camera_value(const Json&);
Light light_value(const Json&);
ParticleEmitter particle_emitter_value(const Json&);
} // namespace forge::detail
