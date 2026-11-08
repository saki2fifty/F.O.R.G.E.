#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <span>
#include <stdexcept>
#include <vector>

namespace forge::geometry_detail {
inline constexpr std::size_t simple_polygon_vertex_limit = 64;
using PolygonPoint = std::array<double, 2>;
using PolygonTriangle = std::array<std::size_t, 3>;
inline double polygon_turn(const PolygonPoint& a, const PolygonPoint& b, const PolygonPoint& c) {
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
}
inline bool polygon_on_segment(const PolygonPoint& a, const PolygonPoint& b,
                               const PolygonPoint& p) {
    return polygon_turn(a, b, p) == 0 && p[0] >= std::min(a[0], b[0]) &&
           p[0] <= std::max(a[0], b[0]) && p[1] >= std::min(a[1], b[1]) &&
           p[1] <= std::max(a[1], b[1]);
}
inline bool polygon_segments_touch(const PolygonPoint& a, const PolygonPoint& b,
                                   const PolygonPoint& c, const PolygonPoint& d) {
    const auto opposite = [](double x, double y) { return (x < 0 && y > 0) || (x > 0 && y < 0); };
    return (opposite(polygon_turn(a, b, c), polygon_turn(a, b, d)) &&
            opposite(polygon_turn(c, d, a), polygon_turn(c, d, b))) ||
           polygon_on_segment(a, b, c) || polygon_on_segment(a, b, d) ||
           polygon_on_segment(c, d, a) || polygon_on_segment(c, d, b);
}
// Pure simple-contour geometry shared by bounded authoring consumers. Callers
// retain their coordinate and capacity policy; no ownership or transform state.
inline double polygon_signed_area_twice(std::span<const PolygonPoint> points) {
    // An origin-relative fan avoids subtracting large almost-equal shoelace
    // terms when a small source contour is offset far from zero.
    double area = 0;
    for (std::size_t i = 1; i + 1 < points.size(); ++i)
        area += polygon_turn(points[0], points[i], points[i + 1]);
    return area;
}
inline double validate_polygon(std::span<const PolygonPoint> points) {
    const auto require = [](bool value, const char* why) {
        if (!value)
            throw std::runtime_error(why);
    };
    require(points.size() >= 3 && points.size() <= simple_polygon_vertex_limit,
            "polygon requires3..64 points");
    for (std::size_t i = 0; i < points.size(); ++i) {
        for (const auto coordinate : points[i])
            require(std::isfinite(coordinate), "polygon coordinates must be finite");
        for (std::size_t j = 0; j < i; ++j)
            require(points[i] != points[j], "polygon repeats a vertex");
        require(polygon_turn(points[(i + points.size() - 1) % points.size()], points[i],
                             points[(i + 1) % points.size()]) != 0,
                "polygon has adjacent collinear vertices");
    }
    for (std::size_t i = 0; i < points.size(); ++i)
        for (std::size_t j = i + 1; j < points.size(); ++j) {
            const auto next_i = (i + 1) % points.size(), next_j = (j + 1) % points.size();
            if (next_i == j || next_j == i)
                continue;
            const auto& a = points[i];
            const auto& b = points[next_i];
            const auto& c = points[j];
            const auto& d = points[next_j];
            require(!polygon_segments_touch(a, b, c, d), "polygon edges intersect or touch");
        }
    const auto area = polygon_signed_area_twice(points);
    require(area > 0, "polygon must have positive counterclockwise area");
    return area;
}
inline std::vector<PolygonTriangle> polygon_triangles(std::span<const PolygonPoint> points) {
    (void)validate_polygon(points);
    const auto require = [](bool value, const char* why) {
        if (!value)
            throw std::runtime_error(why);
    };
    std::vector<std::size_t> ring(points.size());
    std::iota(ring.begin(), ring.end(), 0);
    std::vector<PolygonTriangle> result;
    result.reserve(points.size() - 2);
    while (ring.size() > 3) {
        bool removed = false;
        for (std::size_t i = 0; i < ring.size(); ++i) {
            const auto a = ring[(i + ring.size() - 1) % ring.size()], b = ring[i],
                       c = ring[(i + 1) % ring.size()];
            if (polygon_turn(points[a], points[b], points[c]) <= 0)
                continue;
            const bool occupied = std::any_of(ring.begin(), ring.end(), [&](auto vertex) {
                return vertex != a && vertex != b && vertex != c &&
                       polygon_turn(points[a], points[b], points[vertex]) >= 0 &&
                       polygon_turn(points[b], points[c], points[vertex]) >= 0 &&
                       polygon_turn(points[c], points[a], points[vertex]) >= 0;
            });
            if (occupied)
                continue;
            result.push_back({a, b, c});
            ring.erase(ring.begin() + i);
            removed = true;
            break;
        }
        require(removed, "polygon cannot be decomposed without degenerate pieces");
    }
    require(polygon_turn(points[ring[0]], points[ring[1]], points[ring[2]]) > 0,
            "polygon leaves a degenerate final piece");
    result.push_back({ring[0], ring[1], ring[2]});
    return result;
}
} // namespace forge::geometry_detail
