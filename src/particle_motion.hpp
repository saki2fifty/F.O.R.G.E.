#pragma once
#include <array>
#include <forge/particle_effect.hpp>
#include <optional>

namespace forge {
// Detached emitter-local motion. The emission owner supplies admitted birth values
// and ages; these functions retain no clock, World or per-birth state.
struct ParticleMotionState {
    std::array<double, 3> position{}, velocity{};
    bool operator==(const ParticleMotionState&) const = default;
};
ParticleMotionState evaluate_particle_motion(const ParticleMotionState&,
                                             const std::array<double, 3>& acceleration, double drag,
                                             double age);

// Normalized once at admission, not once for each birth/point query.
ParticleCollisionPlane prepare_particle_collision_plane(const ParticleCollisionPlane&);
struct ParticlePlaneMotion {
    ParticleMotionState state;
    unsigned bounces = 0;
    std::optional<double> death_age;
};
// The plane must come from prepare_particle_collision_plane. Zero allowed bounces
// kills on first incoming contact; exhausting the limit kills at the next one.
// Initially penetrating births die immediately. Sustained zero-speed contact with
// inward acceleration ends the particle instead of inventing a resting-body solver.
ParticlePlaneMotion evaluate_particle_plane_motion(const ParticleMotionState&,
                                                   const std::array<double, 3>& acceleration,
                                                   double drag, double age,
                                                   const ParticleCollisionPlane& prepared);
} // namespace forge
