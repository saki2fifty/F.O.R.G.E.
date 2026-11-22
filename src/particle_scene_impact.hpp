#pragma once
#include <cstdint>
#include <forge/identity.hpp>
#include <forge/transform.hpp>

namespace forge {
enum class ParticleSceneImpactKind : std::uint32_t { Touch, Bounce, End };
// A copied native query outcome. Time is relative to its existing motion step;
// the fixed runtime supplies tick and birth provenance when adopting the step.
struct ParticleSceneImpact {
    double time = 0;
    EntityRef collider;
    Double3 position{}, normal{}, incoming_velocity{}, outgoing_velocity{};
    ParticleSceneImpactKind kind = ParticleSceneImpactKind::Touch;
    bool operator==(const ParticleSceneImpact&) const = default;
};
} // namespace forge
