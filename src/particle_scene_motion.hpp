#pragma once
#include "particle_motion.hpp"
#include <forge/physics_service.hpp>
#include <functional>

namespace forge {
struct ParticleSceneState {
    ParticleMotionState motion;
    unsigned bounces = 0;
    bool alive = true;
    bool operator==(const ParticleSceneState&) const = default;
};
struct ParticleSceneKnot {
    double time;
    ParticleMotionState motion;
};
struct ParticleSceneStep {
    ParticleSceneState state;
    std::vector<ParticleSceneKnot> knots;
    std::optional<double> death;
    unsigned queries = 0;
    double duration = 0;
    // XY contacts leave depth analytic between knots as well as at endpoints.
    Double3 acceleration{};
    double drag = 0;
    bool planar_2d = false;
};
using ParticleSceneQuery =
    std::function<std::optional<PhysicsHit>(const Double3&, const Double3&, double radius)>;
// The existing fixed-update owner supplies admitted world-space motion and the
// native query. Bounded chords approximate curved paths; no World or clock lives
// here. The input remains unchanged on failure. Times are relative to this step.
ParticleSceneStep advance_particle_scene_motion(const ParticleSceneState&,
                                                const Double3& acceleration, double drag, double dt,
                                                const ParticleSceneCollision&,
                                                const ParticleSceneQuery&,
                                                bool initial_probe = false, bool planar_2d = false);
std::optional<ParticleMotionState> sample_particle_scene_motion(const ParticleSceneStep&,
                                                                double time);
} // namespace forge
