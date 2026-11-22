#pragma once
#include <forge/particle_effect.hpp>
namespace forge {
inline constexpr unsigned particle_path_segment_limit = 16;
inline constexpr double particle_path_min_duration = .001, particle_path_max_duration = 30;
// CPU analytic paths use the same seeded birth and lifetime evaluation as heads.
// Tail points remain only until lifetime + duration; point ages stay within life.
// Returned paths have at least two strictly ordered ages and no silent truncation.
ParticlePathSamples sample_particle_paths(const ParticleEffectData&, double seconds,
                                          std::uint32_t seed, double duration, unsigned segments,
                                          std::size_t budget = particle_scene_capacity);
} // namespace forge
