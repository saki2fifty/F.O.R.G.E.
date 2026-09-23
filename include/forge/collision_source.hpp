#pragma once
#include <forge/collision_generation.hpp>
#include <functional>
#include <map>

namespace forge {
// Authored, source-controlled recipe; unknown extension fields remain in the
// document. Mesh references are build inputs, never runtime collision aliases.
struct CollisionSource {
    nlohmann::json document;
    AssetId asset() const;
    static CollisionSource create(AssetId, CollisionKind);
    static CollisionSource from_mesh(AssetId, CollisionKind, AssetRef<MeshAsset>);
    static CollisionSource parse(std::span<const std::byte>);
    void validate() const;
    std::vector<AssetRef<MeshAsset>> mesh_sources() const;
};
struct ResolvedCollisionSource {
    CollisionData data;
    std::map<CollisionMemberId, std::size_t> removed_triangles;
};
ResolvedCollisionSource
resolve_collision_source(const CollisionSource&,
                         const std::function<MeshData(AssetRef<MeshAsset>)>& resolve_mesh,
                         CollisionLimits = {});
} // namespace forge
