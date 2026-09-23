#pragma once
#include <forge/collision_asset.hpp>
#include <forge/resource.hpp>
namespace forge {
namespace physics_detail {
struct PreparedCollision;
}
// Private provider data, never exposed through gameplay SDK callbacks.
struct CollisionResourceData {
    AssetId asset;
    CollisionData geometry;
    std::shared_ptr<const physics_detail::PreparedCollision> native;
    std::size_t resident_bytes = 0;
};
template <> struct ResourceTraits<CollisionAsset> {
    using Data = CollisionResourceData;
};
ResourceCandidate<CollisionAsset> prepare_collision_resource(std::span<const std::byte>,
                                                             std::stop_token = {},
                                                             CollisionLimits = {});
ResourceCandidate<CollisionAsset> prepare_collision_resource(CollisionData, std::stop_token,
                                                             CollisionLimits = {});
} // namespace forge
