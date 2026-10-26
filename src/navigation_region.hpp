#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <forge/navigation_components.hpp>
#include <forge/transform.hpp>
#include <stdexcept>
namespace forge::navigation_detail {
// Pure navigation footprint shared by baking and detached editor consumers.
inline std::array<Double3, 4> region_quad(const NavigationRegion2D& region,
                                          const AffineTransform& transform) {
    if (!std::isfinite(region.width) || !std::isfinite(region.height) || region.width < .01f ||
        region.width > 1000 || region.height < .01f || region.height > 1000)
        throw std::runtime_error("Navigation Region2D dimensions must be.01..1000m");
    const double x = .5 * region.width, y = .5 * region.height;
    std::array<Double3, 4> points{{{-x, -y, 0}, {x, -y, 0}, {x, y, 0}, {-x, y, 0}}};
    for (auto& point : points) {
        point = transform.point(point);
        for (unsigned axis = 0; axis < 2; ++axis)
            if (!std::isfinite(point[axis]) || std::abs(point[axis]) > 4090)
                throw std::runtime_error("XY navigation region exceeds +/-4090m");
        point[2] = 0;
    }
    double area = 0;
    for (unsigned i = 0; i < 4; ++i) {
        const auto& a = points[i];
        const auto& b = points[(i + 1) % 4];
        area += a[0] * b[1] - a[1] * b[0];
    }
    if (!std::isfinite(area) || std::abs(area) < 1e-10)
        throw std::runtime_error("Navigation Region2D has collapsed XY geometry");
    if (area < 0)
        std::reverse(points.begin(), points.end());
    return points;
}
} // namespace forge::navigation_detail
