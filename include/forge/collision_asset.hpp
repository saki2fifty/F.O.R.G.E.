#pragma once
#include <array>
#include <forge/asset_ref.hpp>
#include <span>
#include <vector>

namespace forge {
struct CollisionMemberIdTag;
using CollisionMemberId = PersistentId<CollisionMemberIdTag>;
enum class CollisionKind { Box, Sphere, Capsule, Cylinder, ConvexHull, TriangleMesh, Compound };
// A bounded tree of shapes. Indices are local cooked references, not persistent
// identity. Member IDs survive authoring reorder and identify compound children.
struct CollisionNode {
    CollisionMemberId id;
    CollisionKind kind = CollisionKind::Box;
    std::array<float, 3> translation{}, scale{1, 1, 1};
    std::array<float, 4> rotation{0, 0, 0, 1};
    // Box: full XYZ dimensions. Sphere: radius. Capsule/cylinder: radius,
    // straight cylinder height. Unused entries must be zero.
    std::array<float, 3> dimensions{1, 1, 1};
    std::vector<std::array<float, 3>> vertices;
    std::vector<std::uint32_t> indices, children;
};
struct CollisionData {
    std::vector<CollisionNode> nodes;
    std::uint32_t root = 0;
};
struct CollisionLimits {
    std::size_t bytes = 64 * 1024 * 1024;
    std::size_t vertices = 1024 * 1024, triangles = 1024 * 1024;
    std::size_t hull_points = 65536, nodes = 1024, depth = 32;
};
// Structural/numeric admission only. Native shape Create/IsValidScale is an
// additional required step before publishing an asset or realizing a body.
void validate_collision(const CollisionData&, CollisionLimits = {});
bool collision_contains_triangle_mesh(const CollisionData&);
std::vector<std::byte> encode_collision(const CollisionData&, CollisionLimits = {});
CollisionData decode_collision(std::span<const std::byte>, CollisionLimits = {});
} // namespace forge
