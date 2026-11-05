#pragma once
#include "render_projection.hpp"
#include "tile_grid.hpp"

namespace forge {
struct TileGridPick {
    TileCellAddress cell;
    std::array<double, 2> local;
    double depth = 0; // The same D3D [0,1] projected depth as mesh selection.
};
// Intersect the map's local XY plane, including empty cells for paint tools.
// No inverse of the unused Z axis, triangle expansion or cell scan is required.
inline std::optional<TileGridPick> pick_tile_grid(std::array<double, 2> size,
                                                  const AffineTransform& world,
                                                  const CameraView& view, double x, double y) {
    for (const auto value : size)
        if (!std::isfinite(value) || value <= 0)
            return {};
    const auto hit = pick_render_plane(world, view, x, y);
    if (!hit)
        return {};
    for (unsigned axis = 0; axis < 2; ++axis) {
        const double cell = std::floor(hit->local[axis] / size[axis]);
        if (!std::isfinite(cell) || cell < INT32_MIN || cell > INT32_MAX)
            return {};
    }
    return TileGridPick{tile_cell_at(hit->local, size), hit->local, hit->depth};
}
} // namespace forge
