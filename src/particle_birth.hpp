#pragma once
#include <array>
#include <cstdint>
#include <forge/particle_effect.hpp>
#include <optional>

namespace forge {
struct ParticleMotionState;
// Shared CPU birth and appearance projection for already admitted effects.
// Callers own simulation time and motion; this owner retains no World or clock.
struct ParticleSpawnFrame {
    std::array<double, 3> axis, side, up;
    double cosine;
};
struct ParticleBirthState {
    std::uint64_t ordinal;
    std::array<double, 3> origin{}, velocity{};
};
struct ParticleBirthWindow {
    double phase, units, lifetime;
    std::uint64_t first = 0, last = 0;
};
ParticleSpawnFrame particle_spawn_frame(const ParticleEffectData&);
ParticleBirthState particle_birth_state(const ParticleEffectData&, const ParticleSpawnFrame&,
                                        std::uint32_t seed, std::uint64_t ordinal);
ParticleSample sample_particle_birth(const ParticleEffectData&, const ParticleBirthState&,
                                     double age, const ParticleMotionState* motion = nullptr);
ParticleBirthWindow particle_birth_window(const ParticleEffectData&, double seconds,
                                          double tail = 0);
std::optional<double> particle_birth_age(const ParticleEffectData&, const ParticleBirthWindow&,
                                         double seconds, std::uint64_t ordinal);
} // namespace forge
