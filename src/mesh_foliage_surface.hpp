#pragma once
#include <forge/mesh_resource.hpp>
#include <forge/transform.hpp>
#include <optional>
#include <span>
#include <vector>

namespace forge::detail {
struct FoliageSurfaceSample {
    double height = 0;
    Double3 normal{};
};
// One bounded query over an admitted immutable static Mesh, not a geometry cache.
// World XZ origins select the highest upward-facing LOD0 triangle; ties retain
// part/primitive order. Normals use the existing oriented-area transform, including
// mirrors and supported rank-two surfaces.
std::vector<std::optional<FoliageSurfaceSample>>
mesh_foliage_surfaces(const MeshResourceData&, const AffineTransform&,
                      std::span<const Double2> positions);
} // namespace forge::detail
