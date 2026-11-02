#pragma once
#include "simple_polygon.hpp"
#include "tile_set_values.hpp"
#include <string>

namespace forge::tile_detail {
using TilePolygonPoint = geometry_detail::PolygonPoint;
using TilePolygonTriangle = geometry_detail::PolygonTriangle;
inline double tile_polygon_turn(const TilePolygonPoint& a, const TilePolygonPoint& b,
                                const TilePolygonPoint& c) {
    return geometry_detail::polygon_turn(a, b, c);
}
inline bool tile_polygon_on_segment(const TilePolygonPoint& a, const TilePolygonPoint& b,
                                    const TilePolygonPoint& p) {
    return geometry_detail::polygon_on_segment(a, b, p);
}
inline bool tile_polygon_segments_touch(const TilePolygonPoint& a, const TilePolygonPoint& b,
                                        const TilePolygonPoint& c, const TilePolygonPoint& d) {
    return geometry_detail::polygon_segments_touch(a, b, c, d);
}
// Keep tile source policy separate from the shared contour geometry.
inline std::vector<TilePolygonTriangle>
tile_polygon_triangles(std::span<const TilePolygonPoint> points) {
    if (points.size() < 3 || points.size() > tile_collision_vertex_limit)
        throw std::runtime_error("Tile polygon requires3..8 points");
    for (const auto& point : points)
        for (const auto coordinate : point)
            if (!std::isfinite(coordinate) || coordinate < 0 || coordinate > 1)
                throw std::runtime_error("Tile polygon coordinates must be finite in0..1");
    try {
        return geometry_detail::polygon_triangles(points);
    } catch (const std::runtime_error& error) {
        throw std::runtime_error("Tile " + std::string(error.what()));
    }
}
} // namespace forge::tile_detail
