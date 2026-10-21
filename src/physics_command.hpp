#pragma once
#include <forge/transform.hpp>

namespace forge::physics_detail {
// One copied command representation and FIFO across the two native domains.
// Numeric kinds retain the existing private 3D checkpoint representation.
enum class BodyCommandKind : std::uint32_t {
    Teleport,
    Kinematic,
    Force,
    Impulse,
    Torque,
    AngularImpulse,
    LinearVelocity,
    AngularVelocity
};
struct BodyCommand {
    EntityRef ref;
    LocalTranslation p;
    LocalRotation q;
    BodyCommandKind kind;
    bool clear = false, at_point = false;
    Double3 value{};
    bool planar = false;
};
} // namespace forge::physics_detail
