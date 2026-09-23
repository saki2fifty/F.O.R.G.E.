#pragma once
#include "collision_resource.hpp"
#include <forge/transform.hpp>
namespace forge {
struct PhysicsDebugGeometry {
    // Scaled local geometry in authored-origin coordinates. Apply rotation and
    // translation only at presentation; no live native handles leave this adapter.
    std::vector<std::array<std::array<float, 3>, 3>> triangles;
    bool truncated = false;
};
PhysicsDebugGeometry prepare_physics_debug(const Json& components, LocalScale,
                                           ResourceLease<CollisionAsset> collision = {},
                                           bool crouched = false,
                                           std::size_t triangle_limit = 8192);
} // namespace forge
