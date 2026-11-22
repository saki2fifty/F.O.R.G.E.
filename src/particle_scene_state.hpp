#pragma once
#include "particle_birth.hpp"
#include "particle_path.hpp"
#include "particle_scene_motion.hpp"
#include <forge/transform.hpp>
#include <map>

namespace forge {
struct ParticleSceneBirth {
    ParticleBirthState birth;
    ParticleSceneState state;
    std::array<double, 9> basis;
    Double3 acceleration;
};
struct ParticleSceneTrace {
    ParticleBirthState birth;
    std::array<double, 9> basis;
    double begin = 0;
    ParticleSceneStep step;
};
double particle_scene_trace_start_age(const ParticleEffectData&, const ParticleSceneTrace&,
                                      double previous);
struct ParticleSceneTrailPoint {
    double age = 0;
    Double3 position{};
    bool contact = false;
};
struct ParticleSceneTrailRange {
    std::uint64_t birth = 0;
    double width_scale = 1, end_age = 0;
    std::size_t first = 0, count = 0;
};
// Flat temporal samples belong to the existing Scene history. They do not
// advance emission, reconstruct physics or own another birth registry.
struct ParticleSceneTrailSnapshot {
    double time = 0;
    std::vector<ParticleSceneTrailRange> ranges;
    std::vector<ParticleSceneTrailPoint> points;
};
using ParticleScenePathSamples = ParticleWorldPaths;
// Stored only by ParticleRuntime::Entry. Interval boundaries come from that
// entry's existing emission clock; this value never progresses another clock.
struct ParticleSceneHistory {
    std::map<std::uint64_t, ParticleSceneBirth> live;
    std::vector<ParticleSceneTrace> traces;
    bool initialized = false;
    std::optional<ParticleSceneTrailSnapshot> trail_previous, trail_current;
};
struct ParticleSceneWork {
    std::size_t queries = 0, births = 0;
};
// Construct a detached next interval. The original history is never modified,
// including when a later query or the shared World work budget fails.
ParticleSceneHistory advance_particle_scene_history(const ParticleSceneHistory&,
                                                    const ParticleEffectData&, std::uint32_t seed,
                                                    double previous, double current,
                                                    const AffineTransform& birth_frame,
                                                    const ParticleSceneQuery&, ParticleSceneWork&);
std::vector<ParticleWorldSample> sample_particle_scene_history(const ParticleSceneHistory&,
                                                               const ParticleEffectData&,
                                                               double previous, double current,
                                                               double seconds);
ParticleScenePathSamples sample_particle_scene_paths(const ParticleSceneHistory&,
                                                     const ParticleEffectData&, double previous,
                                                     double current, double seconds);
void retain_particle_scene_trails(ParticleSceneHistory&, const ParticleSceneHistory&,
                                  const ParticleEffectData&, double previous, double current);
} // namespace forge
