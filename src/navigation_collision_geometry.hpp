#pragma once
#include "physics_2d_transform.hpp"
#include "physics_collider_2d.hpp"
#include "simple_polygon.hpp"
#include <forge/navigation_components.hpp>
#include <limits>
#include <numbers>

namespace forge::navigation_detail {
namespace collision_detail {
using Point = geometry_detail::PolygonPoint;
inline std::vector<Point> hull(std::vector<Point> points) {
    std::sort(points.begin(), points.end());
    points.erase(std::unique(points.begin(), points.end()), points.end());
    if (points.size() < 3)
        throw std::runtime_error(
            "Navigation collider footprint collapsed during native conversion");
    std::vector<Point> result;
    result.reserve(points.size() * 2);
    const auto append = [&](const Point& point, std::size_t floor) {
        while (result.size() >= floor &&
               geometry_detail::polygon_turn(result[result.size() - 2], result.back(), point) <= 0)
            result.pop_back();
        result.push_back(point);
    };
    for (const auto& point : points)
        append(point, 2);
    const auto floor = result.size() + 1;
    for (std::size_t i = points.size() - 1; i-- > 0;)
        append(points[i], floor);
    result.pop_back();
    if (result.size() < 3)
        throw std::runtime_error(
            "Navigation collider footprint collapsed during native conversion");
    return result;
}
inline double dot(const Point& a, const Point& b) { return a[0] * b[0] + a[1] * b[1]; }
inline double segment_distance_squared(const Point& point, const Point& a, const Point& b) {
    const Point d{b[0] - a[0], b[1] - a[1]}, p{point[0] - a[0], point[1] - a[1]};
    const auto length = dot(d, d);
    const auto t = length == 0 ? 0 : std::clamp(dot(p, d) / length, 0., 1.);
    const Point v{p[0] - t * d[0], p[1] - t * d[1]};
    return dot(v, v);
}
} // namespace collision_detail

// Detached physical footprint in native-representable world XY. The checked
// physical value/pose owner supplies geometry, never a native world or handles.
inline std::vector<Double3> collider_outline_2d(unsigned kind, std::array<float, 2> raw,
                                                const AffineTransform& transform, bool obstacle) {
    using namespace collision_detail;
    const auto pose = physics_detail::debug_pose_2d(transform);
    const auto dimensions = physics_detail::collider_dimensions_2d(kind, raw, pose.scale);
    const auto frame = physics_detail::collider_rotation_2d(pose.rotation);
    const Point center{pose.translation.x, pose.translation.y};
    const double radius = dimensions[0];
    const float half_height = kind == 2 ? .5f * dimensions[1] : 0;
    const Point delta{-frame[1] * half_height, frame[0] * half_height};
    const Point a{center[0] - delta[0], center[1] - delta[1]},
        b{center[0] + delta[0], center[1] + delta[1]};
    const double extent = kind == 0 ? std::max(dimensions[0], dimensions[1]) : radius + half_height;
    const double magnitude =
        std::max({1., std::abs(center[0]) + extent, std::abs(center[1]) + extent});
    const double margin = 4 * std::numeric_limits<float>::epsilon() * magnitude;
    std::vector<Point> candidates, original_box;
    const auto add = [&](double x, double y) {
        if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) > 4090 || std::abs(y) > 4090)
            throw std::runtime_error("Navigation collider footprint exceeds +/-4090m");
        candidates.push_back({float(x), float(y)});
    };
    if (kind == 0) {
        const double norm = std::hypot(double(frame[0]), double(frame[1]));
        const double padding = (obstacle ? margin : -margin) / norm;
        const double x = .5 * dimensions[0] + padding, y = .5 * dimensions[1] + padding;
        if (!(x > 0 && y > 0))
            throw std::runtime_error("Navigation collider clearance collapsed its footprint");
        for (const auto signs : {Point{-1, -1}, Point{1, -1}, Point{1, 1}, Point{-1, 1}}) {
            const float px = float(signs[0]) * .5f * dimensions[0],
                        py = float(signs[1]) * .5f * dimensions[1];
            original_box.push_back({center[0] + float(frame[0] * px - frame[1] * py),
                                    center[1] + float(frame[1] * px + frame[0] * py)});
            if (std::abs(original_box.back()[0]) > 4090 || std::abs(original_box.back()[1]) > 4090)
                throw std::runtime_error("Navigation collider footprint exceeds +/-4090m");
            add(center[0] + frame[0] * signs[0] * x - frame[1] * signs[1] * y,
                center[1] + frame[1] * signs[0] * x + frame[0] * signs[1] * y);
        }
    } else {
        for (unsigned axis = 0; axis < 2; ++axis)
            if (std::max(std::abs(a[axis]), std::abs(b[axis])) + radius > 4090)
                throw std::runtime_error("Navigation collider footprint exceeds +/-4090m");
        // 62 circle samples leave two vertices for a capsule's segment sum.
        constexpr unsigned count = 62;
        const double adjusted =
            obstacle ? (radius + margin) / std::cos(std::numbers::pi / count) : radius - margin;
        if (!(adjusted > 0))
            throw std::runtime_error("Navigation collider clearance collapsed its footprint");
        for (const auto endpoint : {a, b})
            for (unsigned i = 0; i < count; ++i) {
                const double angle = 2 * std::numbers::pi * i / count;
                add(endpoint[0] + adjusted * std::cos(angle),
                    endpoint[1] + adjusted * std::sin(angle));
            }
    }
    auto outline = hull(std::move(candidates));
    const auto support = [&](const Point& normal) {
        double value = -std::numeric_limits<double>::infinity();
        if (kind == 0) {
            for (const auto& corner : original_box)
                value = std::max(value, dot(normal, corner));
        } else
            value = std::max(dot(normal, a), dot(normal, b)) +
                    radius * std::hypot(normal[0], normal[1]);
        return value;
    };
    // Rounding the translated arcs can expose extra nearly collinear hull
    // vertices. Retain only replacement edges that contain the physical mask;
    // removing a floor vertex always preserves its convex subset.
    // ponytail: at most124 samples bound this scan; revisit if the contour cap grows.
    while (outline.size() > navigation_region_vertex_limit) {
        auto selected = outline.size();
        double least_area = std::numeric_limits<double>::infinity();
        for (std::size_t i = 0; i < outline.size(); ++i) {
            const auto& previous = outline[(i + outline.size() - 1) % outline.size()];
            const auto& point = outline[i];
            const auto& next = outline[(i + 1) % outline.size()];
            const Point normal{next[1] - previous[1], previous[0] - next[0]};
            const double area = geometry_detail::polygon_turn(previous, point, next);
            if (area < least_area && (!obstacle || dot(normal, previous) >= support(normal))) {
                selected = i;
                least_area = area;
            }
        }
        if (selected == outline.size())
            throw std::runtime_error(
                "Navigation collider cannot fit its conservative contour budget");
        outline.erase(outline.begin() + std::ptrdiff_t(selected));
    }
    (void)geometry_detail::validate_polygon(outline);
    // Prove containment after native float rounding. No source is silently
    // admitted with a nonconservative silhouette or a truncated contour.
    if (obstacle) {
        for (std::size_t i = 0; i < outline.size(); ++i) {
            const auto& p = outline[i];
            const auto& q = outline[(i + 1) % outline.size()];
            const Point normal{q[1] - p[1], p[0] - q[0]};
            if (dot(normal, p) < support(normal))
                throw std::runtime_error(
                    "Navigation collider mask cannot contain its physical shape");
        }
    } else {
        for (const auto& point : outline) {
            if (kind == 0) {
                for (unsigned i = 0; i < 4; ++i)
                    if (geometry_detail::polygon_turn(original_box[i], original_box[(i + 1) % 4],
                                                      point) < 0)
                        throw std::runtime_error("Navigation floor exceeds its physical box");
            } else if (segment_distance_squared(point, a, b) > radius * radius)
                throw std::runtime_error("Navigation floor exceeds its physical curved shape");
        }
    }
    std::vector<Double3> result;
    result.reserve(outline.size());
    for (const auto& point : outline)
        result.push_back({point[0], point[1], 0});
    return result;
}
} // namespace forge::navigation_detail
