#pragma once
#include "navigation_tile_cache.hpp"
#include <algorithm>
#include <cmath>

namespace forge::navigation_detail {
// One detached, read-only footprint calculation for runtime and editor consumers.
// It never substitutes a bounding circle for an unsupported affine transform.
inline CylinderObstacle obstacle_cylinder(EntityId id, const NavigationObstacle& obstacle,
                                          const AffineTransform& transform,
                                          const NavigationSettings& settings) {
    const auto require = [](bool value, const char* why) {
        if (!value)
            throw std::runtime_error(why);
    };
    require(std::isfinite(obstacle.radius) && obstacle.radius >= .05f && obstacle.radius <= 5 &&
                std::isfinite(obstacle.height) && obstacle.height >= .2f && obstacle.height <= 10,
            "Navigation obstacle radius must be .05-5 m and height .2-10 m");
    for (double v : transform.m)
        require(std::isfinite(v), "Navigation obstacle requires a finite WorldTransform");
    const auto& m = transform.m;
    const bool xy = settings.plane == NavigationPlane::PlanarXY;
    const Double3 u{m[0], xy ? m[4] : m[8], 0}, v{xy ? m[1] : m[2], xy ? m[5] : m[10], 0};
    const auto a = std::hypot(u[0], u[1]), b = std::hypot(v[0], v[1]);
    require(a > 1e-6 && b > 1e-6 && std::abs(a - b) <= 1e-5 * std::max(a, b) &&
                std::abs(u[0] * v[0] + u[1] * v[1]) <= 1e-5 * a * b,
            "Navigation obstacle requires a circular, unsheared ground-plane scale");
    if (!xy)
        require(std::abs(m[4]) <= 1e-6 * a && std::abs(m[6]) <= 1e-6 * b &&
                    std::abs(m[1]) <= 1e-6 * std::abs(m[5]) &&
                    std::abs(m[9]) <= 1e-6 * std::abs(m[5]) && std::abs(m[5]) > 1e-6,
                "Spatial navigation obstacle must be upright without tilt or shear");
    const double height = xy ? settings.height * 2 : obstacle.height * std::abs(m[5]);
    const double radius = obstacle.radius * a + settings.radius;
    const Double3 center = transform.point({0, 0, 0});
    const auto position = xy ? Double3{center[0], -settings.height, center[1]}
                             : Double3{center[0], center[1] - height / 2, center[2]};
    require(radius > 0 && radius <= 4096 && height > 0 && height <= 8192,
            "Navigation obstacle dimensions exceed supported world bounds");
    CylinderObstacle result{id, {}, float(radius), float(height)};
    for (unsigned axis = 0; axis < 3; ++axis) {
        require(std::abs(position[axis]) <= 4096,
                "Navigation obstacle position exceeds supported world bounds");
        result.position[axis] = float(position[axis]);
    }
    return result;
}
} // namespace forge::navigation_detail
