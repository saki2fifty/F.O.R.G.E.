#pragma once
#include "tile_set_values.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <span>
#include <stdexcept>
#include <vector>

namespace forge::tile_detail {
using TilePolygonPoint = std::array<double, 2>;
using TilePolygonTriangle = std::array<std::size_t, 3>;
inline double tile_polygon_turn(const TilePolygonPoint& a, const TilePolygonPoint& b,
                                const TilePolygonPoint& c) {
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
}
// Source-space contour only: finite unit-cell coordinates, simple CCW order,
// no repeated or adjacent-collinear vertices. Physical float admission follows
// the existing Map/native conversion, after image orientation and scale.
inline std::vector<TilePolygonTriangle>
tile_polygon_triangles(std::span<const TilePolygonPoint> points) {
    const auto require = [](bool value, const char* why) {
        if (!value)
            throw std::runtime_error(why);
    };
    require(points.size() >= 3 && points.size() <= tile_collision_vertex_limit,
            "Tile polygon requires3..8 points");
    for (std::size_t i = 0; i < points.size(); ++i) {
        for (const auto coordinate : points[i])
            require(std::isfinite(coordinate) && coordinate >= 0 && coordinate <= 1,
                    "Tile polygon coordinates must be finite in0..1");
        for (std::size_t j = 0; j < i; ++j)
            require(points[i] != points[j], "Tile polygon repeats a vertex");
        require(tile_polygon_turn(points[(i + points.size() - 1) % points.size()], points[i],
                                  points[(i + 1) % points.size()]) != 0,
                "Tile polygon has adjacent collinear vertices");
    }
    const auto on_segment = [](const TilePolygonPoint& a, const TilePolygonPoint& b,
                               const TilePolygonPoint& point) {
        return point[0] >= std::min(a[0], b[0]) && point[0] <= std::max(a[0], b[0]) &&
               point[1] >= std::min(a[1], b[1]) && point[1] <= std::max(a[1], b[1]);
    };
    for (std::size_t i = 0; i < points.size(); ++i)
        for (std::size_t j = i + 1; j < points.size(); ++j) {
            const auto next_i = (i + 1) % points.size(), next_j = (j + 1) % points.size();
            if (next_i == j || next_j == i)
                continue;
            const auto& a = points[i];
            const auto& b = points[next_i];
            const auto& c = points[j];
            const auto& d = points[next_j];
            const double abc = tile_polygon_turn(a, b, c), abd = tile_polygon_turn(a, b, d),
                         cda = tile_polygon_turn(c, d, a), cdb = tile_polygon_turn(c, d, b);
            const auto opposite = [](double x, double y) {
                return (x < 0 && y > 0) || (x > 0 && y < 0);
            };
            require(!((opposite(abc, abd) && opposite(cda, cdb)) ||
                      (abc == 0 && on_segment(a, b, c)) || (abd == 0 && on_segment(a, b, d)) ||
                      (cda == 0 && on_segment(c, d, a)) || (cdb == 0 && on_segment(c, d, b))),
                    "Tile polygon edges intersect or touch");
        }
    // An origin-relative fan sum avoids subtracting large almost-equal shoelace
    // terms when a small source contour is offset near the cell's corner.
    double area = 0;
    for (std::size_t i = 1; i + 1 < points.size(); ++i)
        area += tile_polygon_turn(points[0], points[i], points[i + 1]);
    require(area > 0, "Tile polygon must have positive counterclockwise area");
    std::vector<std::size_t> ring(points.size());
    std::iota(ring.begin(), ring.end(), 0);
    std::vector<TilePolygonTriangle> result;
    result.reserve(points.size() - 2);
    while (ring.size() > 3) {
        bool removed = false;
        for (std::size_t i = 0; i < ring.size(); ++i) {
            const auto a = ring[(i + ring.size() - 1) % ring.size()], b = ring[i],
                       c = ring[(i + 1) % ring.size()];
            if (tile_polygon_turn(points[a], points[b], points[c]) <= 0)
                continue;
            const bool occupied = std::any_of(ring.begin(), ring.end(), [&](auto vertex) {
                return vertex != a && vertex != b && vertex != c &&
                       tile_polygon_turn(points[a], points[b], points[vertex]) >= 0 &&
                       tile_polygon_turn(points[b], points[c], points[vertex]) >= 0 &&
                       tile_polygon_turn(points[c], points[a], points[vertex]) >= 0;
            });
            if (occupied)
                continue;
            result.push_back({a, b, c});
            ring.erase(ring.begin() + i);
            removed = true;
            break;
        }
        require(removed, "Tile polygon cannot be decomposed without degenerate pieces");
    }
    require(tile_polygon_turn(points[ring[0]], points[ring[1]], points[ring[2]]) > 0,
            "Tile polygon leaves a degenerate final piece");
    result.push_back({ring[0], ring[1], ring[2]});
    return result;
}
} // namespace forge::tile_detail
