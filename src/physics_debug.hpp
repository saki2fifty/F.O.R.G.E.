#pragma once
#include "collision_resource.hpp"
#include <forge/transform.hpp>
#include <nlohmann/json_fwd.hpp>
namespace forge {
// Owner-thread extraction pins only immutable native shape data for a worker.
// ResourceLease itself remains confined to its resource owner's thread.
struct PhysicsDebugCollision {
    AssetId asset;
    std::shared_ptr<const physics_detail::PreparedCollision> native;
};
PhysicsDebugCollision snapshot_physics_debug_collision(const ResourceLease<CollisionAsset>&);
bool physics_debug_uses_character(const nlohmann::json& components);
struct PhysicsDebugGeometry {
    // Scaled local geometry in authored-origin coordinates. Apply rotation and
    // translation only at presentation; no live native handles leave this adapter.
    std::vector<std::array<std::array<float, 3>, 3>> triangles;
    bool truncated = false;
};
PhysicsDebugGeometry prepare_physics_debug(const nlohmann::json& components, LocalScale,
                                           PhysicsDebugCollision collision = {},
                                           bool crouched = false,
                                           std::size_t triangle_limit = 8192);
} // namespace forge
