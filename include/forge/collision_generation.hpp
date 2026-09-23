#pragma once
#include <forge/collision_asset.hpp>
#include <forge/mesh_asset.hpp>

namespace forge {
enum class CollisionDegeneratePolicy { Reject, Remove };
struct CollisionMeshSelection {
    std::uint32_t lod = 0;
    // Explicit indices within the admitted Mesh revision, never asset identity.
    // The caller stores source AssetId/revision and selection in build provenance.
    std::vector<std::uint32_t> parts;
    CollisionKind kind = CollisionKind::TriangleMesh;
    CollisionDegeneratePolicy degenerate = CollisionDegeneratePolicy::Reject;
    bool all_parts = false;
};
struct CollisionGeneration {
    CollisionData data;
    std::size_t removed_triangles = 0;
};
// Uses base POSITION only. Does not silently apply morph/skin animation, create
// a collider component, allocate an AssetId, or publish to the catalog.
CollisionGeneration generate_collision(const MeshData&, const CollisionMeshSelection&,
                                       CollisionMemberId, CollisionLimits = {});
} // namespace forge
