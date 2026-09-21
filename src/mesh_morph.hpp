#pragma once
#include <forge/mesh_asset.hpp>
namespace forge {
// Conservative local-space bounds for an already admitted mesh and a complete
// copied weight vector. Signed weights are valid; no normalization or clamping.
MeshBounds morph_bounds(const MeshPart&, std::span<const float>);
} // namespace forge
