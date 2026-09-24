#pragma once
#include <forge/collision_source.hpp>
#include <forge/derived_cache.hpp>
namespace forge::collision_detail {
inline constexpr char jolt_revision[] = "e77f175595e64cb44218cc9d9d56fc365ad0e36a";
struct Bundle {
    AssetId asset;
    CollisionData geometry;
    std::map<CollisionMemberId, std::size_t> removed_triangles;
};
std::vector<ArtifactFile> encode_bundle(AssetId, const ResolvedCollisionSource&);
Bundle decode_bundle(std::span<const ArtifactFile>, AssetId expected = {});
} // namespace forge::collision_detail
