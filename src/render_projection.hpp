#pragma once
#include <cmath>
#include <forge/render_view.hpp>
#include <optional>
namespace forge {
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
