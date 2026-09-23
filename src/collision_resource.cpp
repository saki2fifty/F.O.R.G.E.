// clang-format off
#include <Jolt/Jolt.h>
// clang-format on
#include "collision_resource.hpp"
#include "collision_shape.hpp"
#include <Jolt/Core/UnorderedSet.h>

namespace forge {
ResourceCandidate<CollisionAsset> prepare_collision_resource(std::span<const std::byte> bytes,
                                                             std::stop_token stop,
                                                             CollisionLimits limits) {
    if (stop.stop_requested())
        throw std::runtime_error("Collision preparation cancelled");
    return prepare_collision_resource(decode_collision(bytes, limits), stop, limits);
}
ResourceCandidate<CollisionAsset>
prepare_collision_resource(CollisionData geometry, std::stop_token stop, CollisionLimits limits) {
    auto cancel = [&] {
        if (stop.stop_requested())
            throw std::runtime_error("Collision preparation cancelled");
    };
    cancel();
    auto value = std::make_unique<CollisionResourceData>();
    value->geometry = std::move(geometry);
    cancel();
    value->native = physics_detail::prepare_collision(value->geometry, limits);
    cancel(); // Native Create is synchronous; never publish a cancelled result.
    JPH::Shape::VisitedShapes visited;
    auto resident = value->native->shape->GetStatsRecursive(visited).mSizeBytes + sizeof(*value) +
                    value->geometry.nodes.capacity() * sizeof(CollisionNode) +
                    sizeof(physics_detail::PreparedCollision) +
                    value->native->members.capacity() * sizeof(CollisionMemberId);
    for (const auto& n : value->geometry.nodes)
        resident += n.vertices.capacity() * sizeof(std::array<float, 3>) +
                    (n.indices.capacity() + n.children.capacity()) * sizeof(std::uint32_t);
    value->resident_bytes = resident;
    return {std::move(value), {resident}};
}
} // namespace forge
