#include "navigation_tile_geometry.hpp"
#include "navigation_collision_geometry.hpp"
#include "physics_collider_2d_native.hpp"
#include "tile_map_collision.hpp"

namespace forge::navigation_detail {
namespace {
using namespace collision_detail;
std::vector<Double3> outline(const b2Polygon& polygon, b2WorldTransform transform, bool obstacle) {
    std::vector<Point> original;
    for (int i = 0; i < polygon.count; ++i) {
        const auto p = b2TransformWorldPoint(transform, polygon.vertices[i]);
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || std::abs(p.x) > 4090 ||
            std::abs(p.y) > 4090)
            throw std::runtime_error("Navigation Tile Map collision exceeds +/-4090m");
        original.push_back({p.x, p.y});
    }
    original = hull(std::move(original));
    (void)geometry_detail::validate_polygon(original);
    double magnitude = 1;
    for (const auto& p : original)
        magnitude = std::max({magnitude, std::abs(p[0]), std::abs(p[1])});
    const double margin = 4 * std::numeric_limits<float>::epsilon() * magnitude;
    struct Plane {
        Point normal;
        double offset;
    };
    std::vector<Plane> planes;
    for (std::size_t i = 0; i < original.size(); ++i) {
        const auto& a = original[i];
        const auto& b = original[(i + 1) % original.size()];
        Point normal{b[1] - a[1], a[0] - b[0]};
        const auto length = std::hypot(normal[0], normal[1]);
        normal[0] /= length;
        normal[1] /= length;
        planes.push_back({normal, dot(normal, a) + (obstacle ? margin : -margin)});
    }
    std::vector<Point> points;
    for (std::size_t i = 0; i < planes.size(); ++i) {
        const auto& a = planes[(i + planes.size() - 1) % planes.size()];
        const auto& b = planes[i];
        const double det = a.normal[0] * b.normal[1] - a.normal[1] * b.normal[0];
        if (!(det > 1e-12))
            throw std::runtime_error("Navigation Tile Map collision corner is too shallow");
        const Point p{(a.offset * b.normal[1] - a.normal[1] * b.offset) / det,
                      (a.normal[0] * b.offset - a.offset * b.normal[0]) / det};
        if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || std::abs(p[0]) > 4090 ||
            std::abs(p[1]) > 4090)
            throw std::runtime_error("Navigation Tile Map collision exceeds +/-4090m");
        points.push_back({float(p[0]), float(p[1])});
    }
    points = hull(std::move(points));
    (void)geometry_detail::validate_polygon(points);
    if (obstacle) {
        for (std::size_t i = 0; i < points.size(); ++i) {
            const auto& a = points[i];
            const auto& b = points[(i + 1) % points.size()];
            const Point normal{b[1] - a[1], a[0] - b[0]};
            for (const auto& p : original)
                if (dot(normal, p) > dot(normal, a))
                    throw std::runtime_error(
                        "Navigation Tile Map mask misses its physical contour");
        }
    } else {
        for (const auto& p : points)
            for (std::size_t i = 0; i < original.size(); ++i)
                if (geometry_detail::polygon_turn(original[i], original[(i + 1) % original.size()],
                                                  p) < 0)
                    throw std::runtime_error(
                        "Navigation Tile Map floor exceeds its physical contour");
    }
    std::vector<Double3> result;
    for (const auto& p : points)
        result.push_back({p[0], p[1], 0});
    return result;
}
} // namespace
RegionContours tile_collider_outlines(const TileMapData& map, const TileSetData& definitions,
                                      const TileMapVariants* variants, std::uint64_t layer,
                                      const AffineTransform& transform, bool obstacle) {
    const auto pose = physics_detail::debug_pose_2d(transform);
    const auto source = tile_map_collision_geometry(map, definitions, layer, 512, variants);
    const auto parts =
        physics_detail::project_tile_collision_2d(source, map.cell_size(), pose.scale);
    const auto rotation = physics_detail::collider_rotation_2d(pose.rotation);
    b2WorldTransform world;
    world.p = {pose.translation.x, pose.translation.y};
    world.q = {rotation[0], rotation[1]};
    RegionContours result;
    result.reserve(parts.boxes.size() + parts.polygons.size());
    for (const auto& box : parts.boxes)
        result.push_back(outline(physics_detail::compound_box(box), world, obstacle));
    for (const auto& polygon : parts.polygons)
        result.push_back(outline(physics_detail::compound_polygon(polygon), world, obstacle));
    return result;
}
} // namespace forge::navigation_detail
