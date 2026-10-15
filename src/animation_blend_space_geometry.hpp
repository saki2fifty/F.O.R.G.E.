#pragma once
#include <algorithm>
#include <cmath>
#include <forge/animation_blend_space.hpp>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

namespace forge::animation_detail {
using BlendPoint = std::array<double, 2>;
using BlendTriangle = std::array<std::size_t, 3>;
using BlendEdge = std::array<std::size_t, 2>;
inline long double blend_orientation(BlendPoint a, BlendPoint b, BlendPoint c) {
    return (static_cast<long double>(b[0]) - a[0]) * (static_cast<long double>(c[1]) - a[1]) -
           (static_cast<long double>(b[1]) - a[1]) * (static_cast<long double>(c[0]) - a[0]);
}
inline BlendEdge blend_edge(std::size_t a, std::size_t b) {
    return {std::min(a, b), std::max(a, b)};
}
inline bool blend_circle_contains(const std::vector<BlendPoint>& points, BlendTriangle t,
                                  BlendPoint p) {
    const long double ax = static_cast<long double>(points[t[0]][0]) - p[0];
    const long double ay = static_cast<long double>(points[t[0]][1]) - p[1];
    const long double bx = static_cast<long double>(points[t[1]][0]) - p[0];
    const long double by = static_cast<long double>(points[t[1]][1]) - p[1];
    const long double cx = static_cast<long double>(points[t[2]][0]) - p[0];
    const long double cy = static_cast<long double>(points[t[2]][1]) - p[1];
    const auto a = (ax * ax + ay * ay) * (bx * cy - by * cx);
    const auto b = (bx * bx + by * by) * (ax * cy - ay * cx);
    const auto c = (cx * cx + cy * cy) * (ax * by - ay * bx);
    const auto tolerance =
        64 * std::numeric_limits<double>::epsilon() * (std::abs(a) + std::abs(b) + std::abs(c));
    // A consistent strict boundary rule resolves cocircular ties in sorted order.
    return a - b + c > tolerance;
}
inline void prepare_blend_geometry(AnimationBlendSpace& space) {
    auto minimum = BlendPoint{space.samples.front().position, space.samples.front().position_y};
    auto maximum = minimum;
    for (const auto& sample : space.samples) {
        minimum[0] = std::min(minimum[0], sample.position);
        minimum[1] = std::min(minimum[1], sample.position_y);
        maximum[0] = std::max(maximum[0], sample.position);
        maximum[1] = std::max(maximum[1], sample.position_y);
    }
    space.origin = minimum;
    space.extent = std::max(maximum[0] - minimum[0], maximum[1] - minimum[1]);
    if (space.extent == 0)
        space.extent = 1;
    std::set<BlendPoint> unique;
    for (const auto& sample : space.samples) {
        const BlendPoint point{(sample.position - minimum[0]) / space.extent,
                               (sample.position_y - minimum[1]) / space.extent};
        if (!unique.insert(point).second)
            throw std::runtime_error("Blend Space sample spacing cannot be represented reliably");
        space.points.push_back(point);
    }
    if (space.points.size() == 1)
        return;
    // Work in normalized coordinates and discard the enclosing triangle at the end.
    auto points = space.points;
    const auto count = points.size();
    points.push_back({-16, -8});
    points.push_back({16, -8});
    points.push_back({0, 16});
    std::vector<BlendTriangle> triangles{{count, count + 1, count + 2}};
    for (std::size_t i = 0; i < count; ++i) {
        std::map<BlendEdge, unsigned> edges;
        std::vector<BlendTriangle> retained;
        for (const auto& triangle : triangles) {
            if (!blend_circle_contains(points, triangle, points[i])) {
                retained.push_back(triangle);
                continue;
            }
            for (unsigned j = 0; j < 3; ++j)
                ++edges[blend_edge(triangle[j], triangle[(j + 1) % 3])];
        }
        if (edges.empty())
            throw std::runtime_error("Blend Space triangulation could not admit a sample");
        for (const auto& [edge, uses] : edges) {
            if (uses != 1)
                continue;
            BlendTriangle triangle{edge[0], edge[1], i};
            const auto area =
                blend_orientation(points[triangle[0]], points[triangle[1]], points[i]);
            if (area == 0)
                continue;
            if (area < 0)
                std::swap(triangle[0], triangle[1]);
            retained.push_back(triangle);
        }
        triangles = std::move(retained);
    }
    for (auto triangle : triangles) {
        if (*std::max_element(triangle.begin(), triangle.end()) >= count)
            continue;
        const auto first = std::min_element(triangle.begin(), triangle.end());
        std::rotate(triangle.begin(), first, triangle.end());
        space.triangles.push_back(triangle);
    }
    std::sort(space.triangles.begin(), space.triangles.end());
    if (space.triangles.empty()) {
        // A point or straight line is a useful degenerate blend space as well.
        for (const auto& point : space.points)
            if (blend_orientation(space.points.front(), space.points.back(), point) != 0)
                throw std::runtime_error("Blend Space triangulation is numerically unstable");
        for (std::size_t i = 1; i < count; ++i)
            space.boundary.push_back({i - 1, i});
        return;
    }
    std::map<BlendEdge, unsigned> edges;
    std::set<std::size_t> covered;
    for (const auto& triangle : space.triangles)
        for (unsigned j = 0; j < 3; ++j) {
            covered.insert(triangle[j]);
            ++edges[blend_edge(triangle[j], triangle[(j + 1) % 3])];
        }
    if (covered.size() != count)
        throw std::runtime_error("Blend Space triangulation omitted an unstable sample");
    for (const auto& [edge, uses] : edges) {
        if (uses > 2)
            throw std::runtime_error("Blend Space triangulation has an invalid shared edge");
        if (uses == 1)
            space.boundary.push_back(edge);
    }
    if (space.boundary.empty())
        throw std::runtime_error("Blend Space triangulation has no outer boundary");
}
inline AnimationBlendSelection blend_selection(BlendTriangle ids, std::array<double, 3> weights) {
    AnimationBlendSelection result;
    double total = 0;
    for (const auto weight : weights)
        total += std::max(0., weight);
    if (!std::isfinite(total) || total <= 0)
        throw std::runtime_error("Blend Space weights could not be normalized");
    for (unsigned i = 0; i < 3; ++i)
        if (weights[i] > 0) {
            result.samples[result.count] = ids[i];
            result.weights[result.count++] = weights[i] / total;
        }
    return result;
}
inline AnimationBlendSelection select_blend_geometry(const AnimationBlendSpace& space, double x,
                                                     double y) {
    const BlendPoint point{(x - space.origin[0]) / space.extent,
                           (y - space.origin[1]) / space.extent};
    if (!std::isfinite(point[0]) || !std::isfinite(point[1]))
        throw std::runtime_error("Blend Space query exceeds representable coordinate range");
    if (space.points.size() == 1)
        return {{0, 0, 0}, {1, 0, 0}, 1};
    for (const auto& triangle : space.triangles) {
        const auto a = space.points[triangle[0]], b = space.points[triangle[1]],
                   c = space.points[triangle[2]];
        const auto area = blend_orientation(a, b, c);
        const double first = double(blend_orientation(point, b, c) / area);
        const double second = double(blend_orientation(a, point, c) / area);
        const double third = 1 - first - second;
        if (first >= -1e-12 && second >= -1e-12 && third >= -1e-12)
            return blend_selection(triangle, {first, second, third});
    }
    double best = std::numeric_limits<double>::infinity();
    AnimationBlendSelection selected;
    for (const auto& edge : space.boundary) {
        const auto a = space.points[edge[0]], b = space.points[edge[1]];
        const double dx = b[0] - a[0], dy = b[1] - a[1];
        const double length = dx * dx + dy * dy;
        if (!std::isfinite(length) || length <= 0)
            throw std::runtime_error("Blend Space boundary spacing is numerically unstable");
        const double parameter =
            std::clamp(((point[0] - a[0]) * dx + (point[1] - a[1]) * dy) / length, 0., 1.);
        const double px = a[0] + parameter * dx - point[0], py = a[1] + parameter * dy - point[1];
        const double distance = px * px + py * py;
        if (distance < best) {
            best = distance;
            selected = blend_selection({edge[0], edge[1], 0}, {1 - parameter, parameter, 0});
        }
    }
    if (!std::isfinite(best) || !selected.count)
        throw std::runtime_error("Blend Space query could not select a stable boundary");
    return selected;
}
} // namespace forge::animation_detail
