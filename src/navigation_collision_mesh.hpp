#pragma once
#include "physics_debug.hpp"

namespace forge::navigation_detail {
struct NavigationCollisionGeometry {
    AffineTransform rigid;
    PhysicsDebugGeometry shape;
};
// Share physical shape preparation/triangle extraction; no simulation world or
// mutable native body is involved. The caller retains its normal source budget.
NavigationCollisionGeometry collision_geometry(const nlohmann::json& shape_components,
                                               const AffineTransform& world,
                                               PhysicsDebugCollision collision,
                                               std::size_t triangle_limit);
} // namespace forge::navigation_detail
