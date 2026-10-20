#pragma once
#include "render_projection.hpp"
#include "tile_grid.hpp"
#include <algorithm>

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
    if (!std::isfinite(x) || !std::isfinite(y) || x < view.viewport.x || y < view.viewport.y ||
        x >= double(view.viewport.x) + view.viewport.width ||
        y >= double(view.viewport.y) + view.viewport.height)
        return {};
    for (const auto value : size)
        if (!std::isfinite(value) || value <= 0)
            return {};
    for (const auto value : world.m)
        if (!std::isfinite(value))
            return {};
    auto relative = view;
    relative.position = {};
    const auto near_point = unproject_render_point(relative, x, y, 0);
    const auto far_point = unproject_render_point(relative, x, y, 1);
    if (!near_point || !far_point)
        return {};
    const auto cross = [](Double3 a, Double3 b) -> Double3 {
        return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    };
    const auto dot = [](Double3 a, Double3 b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
    Double3 u{}, v{}, origin{}, direction{}, to_plane{};
    double scale_u = 0, scale_v = 0;
    for (unsigned axis = 0; axis < 3; ++axis) {
        u[axis] = world.m[axis * 4];
        v[axis] = world.m[axis * 4 + 1];
        scale_u = std::max(scale_u, std::abs(u[axis]));
        scale_v = std::max(scale_v, std::abs(v[axis]));
        origin[axis] = world.m[axis * 4 + 3] - view.position[axis];
        direction[axis] = (*far_point)[axis] - (*near_point)[axis];
        to_plane[axis] = origin[axis] - (*near_point)[axis];
    }
    if (scale_u == 0 || scale_v == 0)
        return {};
    for (unsigned axis = 0; axis < 3; ++axis) {
        u[axis] /= scale_u;
        v[axis] /= scale_v;
    }
    const auto normal = cross(u, v);
    const double area = dot(normal, normal), divisor = dot(normal, direction);
    if (!(area > 0) || divisor == 0)
        return {};
    const double t = dot(normal, to_plane) / divisor;
    if (!std::isfinite(t) || t < 0 || t > 1)
        return {};
    Double3 point{}, offset{};
    for (unsigned axis = 0; axis < 3; ++axis) {
        point[axis] = (*near_point)[axis] + t * direction[axis];
        offset[axis] = point[axis] - origin[axis];
    }
    const std::array<double, 2> local{dot(cross(offset, v), normal) / area / scale_u,
                                      dot(cross(u, offset), normal) / area / scale_v};
    const double z = dot(point, view.forward);
    const double w = view.projection[14] * z + view.projection[15];
    const double depth = (view.projection[10] * z + view.projection[11]) / w;
    if (!(w > 0) || !std::isfinite(depth) || depth < 0 || depth > 1)
        return {};
    for (unsigned axis = 0; axis < 2; ++axis) {
        const double cell = std::floor(local[axis] / size[axis]);
        if (!std::isfinite(cell) || cell < INT32_MIN || cell > INT32_MAX)
            return {};
    }
    return TileGridPick{tile_cell_at(local, size), local, depth};
}
} // namespace forge
