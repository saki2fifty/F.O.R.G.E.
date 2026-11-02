#include "physics_collider_2d_native.hpp"
#include "tile_map_collision.hpp"
#include <span>

namespace forge::physics_detail {
b2Polygon compound_box(const BoxPart2D& value) {
    for (unsigned axis = 0; axis < 2; ++axis) {
        (void)collider_dimension_2d(value.dimensions[axis], .01f, 1000);
        if (!std::isfinite(value.center[axis]) ||
            std::abs(double(value.center[axis])) + .5 * value.dimensions[axis] > 10000)
            throw std::runtime_error("2D compound box exceeds supported local extent");
    }
    return b2MakeOffsetBox(value.dimensions[0] * .5f, value.dimensions[1] * .5f,
                           {value.center[0], value.center[1]}, {1, 0});
}
b2Polygon compound_polygon(const PolygonPart2D& value) {
    static_assert(tile_collision_vertex_limit == B2_MAX_POLYGON_VERTICES);
    if (value.count < 3 || value.count > tile_collision_vertex_limit)
        throw std::runtime_error("2D compound polygon requires3..8 vertices");
    std::array<b2Vec2, tile_collision_vertex_limit> points{};
    std::array<float, 2> low{10000, 10000}, high{-10000, -10000};
    for (std::uint32_t i = 0; i < value.count; ++i) {
        for (unsigned axis = 0; axis < 2; ++axis) {
            const auto scalar = value.points[i][axis];
            if (!std::isfinite(scalar) || std::abs(scalar) > 10000)
                throw std::runtime_error("2D compound polygon exceeds supported local extent");
            low[axis] = std::min(low[axis], scalar);
            high[axis] = std::max(high[axis], scalar);
        }
        points[i] = {value.points[i][0], value.points[i][1]};
    }
    for (unsigned axis = 0; axis < 2; ++axis)
        (void)collider_dimension_2d(double(high[axis]) - low[axis], .01f, 1000);
    if (!tile_detail::tile_collision_winding<float>(std::span(value.points).first(value.count)))
        throw std::runtime_error("2D compound polygon contour is not strictly convex");
    const auto hull = b2ComputeHull(points.data(), int(value.count));
    if (hull.count != int(value.count) || !b2ValidateHull(&hull))
        throw std::runtime_error("2D compound polygon degenerates at its physical scale");
    return b2MakePolygon(&hull, 0);
}
ColliderParts2D project_tile_collision_2d(const TileMapCollisionGeometry& geometry,
                                          std::array<double, 2> size, LocalScale scale) {
    if (!std::isfinite(scale.x) || !std::isfinite(scale.y) || !std::isfinite(scale.z))
        throw std::runtime_error("2D collider scale must be finite");
    if (scale.x == 0 || scale.y == 0)
        throw std::runtime_error("Tile Map collision requires nonzero XY scale");
    const auto& rectangles = geometry.rectangles;
    std::vector<BoxPart2D> values;
    values.reserve(rectangles.size());
    for (const auto& rectangle : rectangles) {
        const std::array<std::int32_t, 2> origin{rectangle.origin.x, rectangle.origin.y};
        const std::array<std::uint32_t, 2> counts{rectangle.width, rectangle.height};
        const std::array<float, 2> axes{scale.x, scale.y};
        BoxPart2D box;
        for (unsigned axis = 0; axis < 2; ++axis) {
            const double full = double(counts[axis]) * size[axis] * std::abs(axes[axis]);
            const double center =
                (double(origin[axis]) + .5 * counts[axis]) * size[axis] * axes[axis];
            box.dimensions[axis] = collider_dimension_2d(full, .01f, 1000);
            if (!std::isfinite(center) || std::abs(center) + .5 * full > 10000)
                throw std::runtime_error(
                    "Tile Map collision exceeds supported scaled local extent");
            box.center[axis] = float(center);
        }
        (void)compound_box(box);
        values.push_back(box);
    }
    ColliderParts2D result;
    result.boxes = std::move(values);
    if (!geometry.polygons.empty()) {
        std::vector<PolygonPart2D> polygons;
        polygons.reserve(geometry.polygons.size());
        for (const auto& cell : geometry.polygons) {
            PolygonPart2D polygon;
            polygon.count = std::uint32_t(cell.count);
            polygon.boundary_edges = cell.boundary_edges;
            for (std::size_t i = 0; i < cell.count; ++i)
                for (unsigned axis = 0; axis < 2; ++axis) {
                    const double origin = axis == 0 ? cell.origin.x : cell.origin.y;
                    const double point = (origin + cell.points[i][axis]) * size[axis] *
                                         (axis == 0 ? scale.x : scale.y);
                    if (!std::isfinite(point) || std::abs(point) > 10000)
                        throw std::runtime_error(
                            "Tile polygon exceeds supported scaled local extent");
                    polygon.points[i][axis] = float(point);
                }
            (void)compound_polygon(polygon);
            polygons.push_back(polygon);
        }
        result.polygons = std::move(polygons);
    }
    return result;
}
} // namespace forge::physics_detail
