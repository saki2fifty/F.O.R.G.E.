#pragma once
#include <array>
#include <forge/physics_components.hpp>
#include <forge/world.hpp>
#include <map>

namespace forge::physics_detail {
struct Body2DDesired;
struct Joint2DDesired {
    EntityRef ref;
    PhysicsJoint2D config;
    std::uint64_t a = 0, b = 0;
    std::array<float, 2> anchor_a{}, anchor_b{};
};
using DesiredJoints2D = std::map<std::uint64_t, Joint2DDesired>;
// Detached source validation only. PhysicsRuntime owns the combined joint budget
// and invokes this before mutating either native domain or authored command poses.
DesiredJoints2D desired_joints_2d(WorldContext&, const std::map<std::uint64_t, Body2DDesired>&,
                                  std::uint64_t tick);
bool joint_same_geometry_2d(const Joint2DDesired&, const Joint2DDesired&);
bool joint_controls_changed_2d(const PhysicsJoint2D&, const PhysicsJoint2D&);
} // namespace forge::physics_detail
