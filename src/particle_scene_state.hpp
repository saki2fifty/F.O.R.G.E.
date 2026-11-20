#pragma once
#include "particle_birth.hpp"
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
// Stored only by ParticleRuntime::Entry. Interval boundaries come from that
// entry's existing emission clock; this value never progresses another clock.
struct ParticleSceneHistory {
    std::map<std::uint64_t, ParticleSceneBirth> live;
    std::vector<ParticleSceneTrace> traces;
    bool initialized = false;
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
} // namespace forge
