#pragma once
#include <cmath>
#include <forge/render_view.hpp>
#include <limits>
namespace forge {
// Native EnvMapRenderer only needs a homogeneous point along each sky ray.
// Encode a unit-depth, camera-relative ray plane instead of unprojecting depth1:
// the latter has w=0 for an infinite-far perspective and loses precision at large
// world positions. This is transient ray reconstruction, never an authored matrix.
inline std::array<float, 16> sky_ray_matrix(const CameraView& view) {
    std::array<float, 16> result{};
    const bool perspective = view.projection[15] == 0;
    if (perspective && (view.projection[0] == 0 || view.projection[5] == 0))
        throw std::runtime_error("Sky camera projection has no horizontal or vertical extent");
    for (unsigned axis = 0; axis < 3; ++axis) {
        const double right = perspective ? view.right[axis] / double(view.projection[0]) : 0;
        const double up = perspective ? view.up[axis] / double(view.projection[5]) : 0;
        for (const auto value : {right, up, view.forward[axis]})
            if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
                throw std::runtime_error("Sky camera direction exceeds finite GPU representation");
        result[axis] = float(right);
        result[4 + axis] = float(up);
        result[12 + axis] = float(view.forward[axis]);
    }
    result[15] = 1;
    return result;
}
} // namespace forge
