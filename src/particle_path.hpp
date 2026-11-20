#pragma once
#include <forge/particle_effect.hpp>
namespace forge {
inline constexpr unsigned particle_path_segment_limit = 16;
inline constexpr double particle_path_min_duration = .001, particle_path_max_duration = 30;
struct ParticlePathRange {
    std::uint64_t birth;
    std::size_t first, count;
};
// Detached points share contiguous frame storage; there is no per-birth history owner.
struct ParticlePathSamples {
    std::vector<ParticlePathRange> paths;
    std::vector<ParticleSample> points;
    std::size_t segment_count() const { return points.size() - paths.size(); }
};
// CPU analytic paths use the same seeded birth and lifetime evaluation as heads.
// Tail points remain only until lifetime + duration; point ages stay within life.
// Returned paths have at least two strictly ordered ages and no silent truncation.
ParticlePathSamples sample_particle_paths(const ParticleEffectData&, double seconds,
                                          std::uint32_t seed, double duration, unsigned segments,
                                          std::size_t budget = particle_scene_capacity);
} // namespace forge
