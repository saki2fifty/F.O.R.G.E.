#pragma once
// Internal Jolt adapter. Include Jolt/Jolt.h first; never install this header.
#include "physics_registration.hpp"
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <forge/collision_asset.hpp>
namespace forge::physics_detail {
struct PreparedCollision {
    // Registration must outlive shape destruction, including worker results.
    std::shared_ptr<Registration> registration_lease;
    JPH::RefConst<JPH::Shape> shape;
    bool static_only = false;
    std::vector<CollisionMemberId> members;
};
std::shared_ptr<const PreparedCollision> prepare_collision(const CollisionData&,
                                                           CollisionLimits = {});
} // namespace forge::physics_detail
