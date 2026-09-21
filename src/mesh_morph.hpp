#pragma once
#include <forge/mesh_asset.hpp>
#include <optional>
namespace forge {
// Immutable intervals prepared once from admitted vertex deltas. Evaluating a
// new weight vector is O(targets), independent of vertex count.
struct MorphBoundsData {
    MeshBounds base;
    std::vector<std::optional<MeshBounds>> positions;
};
MorphBoundsData prepare_morph_bounds(const MeshPart&);
MeshBounds morph_bounds(const MorphBoundsData&, std::span<const float>);
// Conservative local-space bounds for an already admitted mesh and a complete
// copied weight vector. Signed weights are valid; no normalization or clamping.
MeshBounds morph_bounds(const MeshPart&, std::span<const float>);
} // namespace forge
