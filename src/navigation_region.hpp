#pragma once
#include "simple_polygon.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <forge/navigation_components.hpp>
#include <forge/transform.hpp>
#include <stdexcept>
namespace forge::navigation_detail {
static_assert(navigation_region_vertex_limit <= geometry_detail::simple_polygon_vertex_limit);
inline void validate_region_dimensions(const NavigationRegion2D& region) {
    if (region.source > 1)
        throw std::runtime_error("Unsupported Navigation Region shape source");
    if (!std::isfinite(region.width) || !std::isfinite(region.height) || region.width < .01f ||
        region.width > 1000 || region.height < .01f || region.height > 1000)
        throw std::runtime_error("Navigation Region2D dimensions must be.01..1000m");
}
// Local source policy is shared by codecs, baking and detached editor views.
inline std::vector<geometry_detail::PolygonPoint>
region_local_points(const NavigationRegion2D& region) {
    validate_region_dimensions(region);
    if (region.points.empty()) {
        const double x = .5 * region.width, y = .5 * region.height;
        return {{-x, -y}, {x, -y}, {x, y}, {-x, y}};
    }
    if (region.points.size() < 3 || region.points.size() > navigation_region_vertex_limit)
        throw std::runtime_error("Navigation polygon requires3..64 points");
    std::vector<geometry_detail::PolygonPoint> points;
    points.reserve(region.points.size());
    for (const auto& point : region.points) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || std::abs(point.x) > 1000 ||
            std::abs(point.y) > 1000)
            throw std::runtime_error("Navigation polygon coordinates must be finite in+/-1000m");
        points.push_back({point.x, point.y});
    }
    (void)geometry_detail::validate_polygon(points);
    return points;
}
// Pure navigation footprint shared by baking and detached editor consumers.
inline std::array<Double3, 4> region_quad(const NavigationRegion2D& region,
                                          const AffineTransform& transform) {
    validate_region_dimensions(region);
    if (region.source != 0 || !region.points.empty())
        throw std::runtime_error("Polygon navigation regions do not have rectangle extents");
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
// Resolved XY outline; visual Z does not contribute to navigation geometry.
inline std::vector<Double3> region_outline(const NavigationRegion2D& region,
                                           const AffineTransform& transform) {
    if (region.source != 0)
        throw std::runtime_error("Collider navigation requires its selected physical footprint");
    if (region.points.empty()) {
        const auto quad = region_quad(region, transform);
        return {quad.begin(), quad.end()};
    }
    const auto local = region_local_points(region);
    std::vector<Double3> points;
    points.reserve(local.size());
    for (const auto& value : local) {
        auto point = transform.point({value[0], value[1], 0});
        for (unsigned axis = 0; axis < 2; ++axis)
            if (!std::isfinite(point[axis]) || std::abs(point[axis]) > 4090)
                throw std::runtime_error("XY navigation region exceeds +/-4090m");
        point[2] = 0;
        points.push_back(point);
    }
    double area = 0;
    for (std::size_t i = 1; i + 1 < points.size(); ++i)
        area += (points[i][0] - points[0][0]) * (points[i + 1][1] - points[0][1]) -
                (points[i][1] - points[0][1]) * (points[i + 1][0] - points[0][0]);
    if (!std::isfinite(area) || std::abs(area) < 1e-10)
        throw std::runtime_error("Navigation Region2D has collapsed XY geometry");
    if (area < 0)
        std::reverse(points.begin(), points.end());
    return points;
}
} // namespace forge::navigation_detail
