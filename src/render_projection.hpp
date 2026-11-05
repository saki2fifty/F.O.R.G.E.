#pragma once
#include <algorithm>
#include <cmath>
#include <forge/render_view.hpp>
#include <limits>
#include <optional>
#include <stdexcept>
namespace forge {
// Shared forward depth admission for spatial passes. Projection maps positive
// view distance into D3D [0,1]; finite near and finite/infinite far are supported.
inline void validate_forward_depth_projection(const std::array<float, 4>& p) {
    if (!std::all_of(p.begin(), p.end(), [](float x) { return std::isfinite(x); }) ||
        double(p[0]) * p[3] <= double(p[1]) * p[2])
        throw std::invalid_argument("Forward depth projection must be finite and increasing");
    const auto distance = [&](double depth) {
        return (double(p[1]) - depth * p[3]) / (depth * p[2] - p[0]);
    };
    const auto near_distance = distance(0);
    const auto far_distance = p[2] == p[0] ? std::numeric_limits<double>::infinity() : distance(1);
    if (!std::isfinite(near_distance) || std::isnan(far_distance) || near_distance <= 0 ||
        far_distance <= near_distance)
        throw std::invalid_argument("Forward depth projection requires positive near/far");
}
// Current spatial inputs use the complete camera projection, including jitter.
inline void validate_surface_projection(const std::array<float, 16>& p) {
    if (!std::all_of(p.begin(), p.end(), [](float x) { return std::isfinite(x); }) || p[0] == 0 ||
        p[5] == 0 || p[1] != 0 || p[4] != 0 || p[8] != 0 || p[9] != 0 || p[12] != 0 || p[13] != 0)
        throw std::invalid_argument("Spatial surface projection requires admitted camera axes");
    validate_forward_depth_projection({p[10], p[11], p[14], p[15]});
}
// Inverse of FORGE's admitted perspective/orthographic camera projection at
// D3D depth [0,1]. Uses the same float projection as picking and rendering.
inline std::optional<Double3> unproject_render_point(const CameraView& view, double x, double y,
                                                     double depth) {
    if (!view.viewport.width || !view.viewport.height || !std::isfinite(x) || !std::isfinite(y) ||
        !std::isfinite(depth) || depth < 0 || depth > 1)
        return {};
    const auto& p = view.projection;
    const double divisor = depth * p[14] - p[10];
    if (divisor == 0 || p[0] == 0 || p[5] == 0)
        return {};
    const double z = (p[11] - depth * p[15]) / divisor, w = p[14] * z + p[15];
    if (!std::isfinite(z) || !std::isfinite(w) || w <= 0)
        return {};
    const double px = ((x - view.viewport.x) * 2 / view.viewport.width - 1) * w / p[0];
    const double py = (1 - (y - view.viewport.y) * 2 / view.viewport.height) * w / p[5];
    Double3 result;
    for (unsigned i = 0; i < 3; ++i) {
        result[i] = view.position[i] + view.right[i] * px + view.up[i] * py + view.forward[i] * z;
        if (!std::isfinite(result[i]))
            return {};
    }
    return result;
}
struct RenderPlanePick {
    Double2 local;
    double depth = 0; // The shared D3D [0,1] projected depth.
};
// Intersect an affine local XY plane in physical view coordinates. The unused
// Z axis need not be invertible. Camera-relative math retains large-world precision.
inline std::optional<RenderPlanePick>
pick_render_plane(const AffineTransform& world, const CameraView& view, double x, double y) {
    if (!std::isfinite(x) || !std::isfinite(y) || x < view.viewport.x || y < view.viewport.y ||
        x >= double(view.viewport.x) + view.viewport.width ||
        y >= double(view.viewport.y) + view.viewport.height)
        return {};
    for (const auto value : world.m)
        if (!std::isfinite(value))
            return {};
    auto relative = view;
    relative.position = {};
    const auto near_point = unproject_render_point(relative, x, y, 0);
    auto far_point = unproject_render_point(relative, x, y, 1);
    const bool finite_segment = far_point.has_value();
    // Infinite-far perspective has no finite point at depth one. A second
    // point inside the clip range defines the same forward ray.
    if (!far_point)
        far_point = unproject_render_point(relative, x, y, .5);
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
    if (!std::isfinite(t) || t < 0 || (finite_segment && t > 1))
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
    return RenderPlanePick{local, depth};
}
// Camera-relative double projection for editor/debug overlays. Result uses the
// physical target's pixel coordinates, including the camera viewport offset.
inline std::optional<std::array<double, 2>> project_render_point(const CameraView& view,
                                                                 Double3 point) {
    Double3 delta;
    for (unsigned i = 0; i < 3; ++i)
        delta[i] = point[i] - view.position[i];
    auto dot = [&](const Double3& axis) {
        return delta[0] * axis[0] + delta[1] * axis[1] + delta[2] * axis[2];
    };
    const std::array<double, 4> local{dot(view.right), dot(view.up), dot(view.forward), 1};
    std::array<double, 4> clip{};
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned col = 0; col < 4; ++col)
            clip[row] += view.projection[row * 4 + col] * local[col];
    for (const auto value : clip)
        if (!std::isfinite(value))
            return {};
    if (!(clip[3] > 0) || clip[2] < 0 || clip[2] > clip[3])
        return {};
    const double x = view.viewport.x + (clip[0] / clip[3] + 1) * .5 * view.viewport.width;
    const double y = view.viewport.y + (1 - clip[1] / clip[3]) * .5 * view.viewport.height;
    if (!std::isfinite(x) || !std::isfinite(y))
        return {};
    return std::array<double, 2>{x, y};
}
} // namespace forge
