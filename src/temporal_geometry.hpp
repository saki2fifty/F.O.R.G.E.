#pragma once
#include <forge/identity.hpp>
#include <vector>

namespace forge {
// Derived visibility in one completed camera view. Entity and resource ownership
// stay with the existing scene renderer; these keys only gate correspondence.
struct TemporalMeshPart {
    EntityId entity;
    unsigned lod{}, part{};
    auto operator<=>(const TemporalMeshPart&) const = default;
};
inline constexpr std::size_t temporal_mesh_part_limit = 65536;
// Sorted after staging. Two view-owned vectors swap at completion and retain
// capacity, rather than allocating a tree node for every part on every frame.
using TemporalMeshParts = std::vector<TemporalMeshPart>;
} // namespace forge
