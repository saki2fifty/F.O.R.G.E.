#pragma once
#include "simple_polygon.hpp"
#include <limits>

// Pure contour geometry shared by authoring consumers. Callers retain coordinate,
// capacity, winding admission and source ownership. Decomposition adds no points.
namespace forge::geometry_detail::contour_detail {
using Point = PolygonPoint;
using Loop = std::vector<Point>;
using Triangle = std::array<Point, 3>;
inline double turn(const Point& a, const Point& b, const Point& c) { return polygon_turn(a, b, c); }
inline void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
inline bool on(const Point& a, const Point& b, const Point& p) {
    return polygon_on_segment(a, b, p);
}
inline bool touches(const Point& a, const Point& b, const Point& c, const Point& d) {
    return polygon_segments_touch(a, b, c, d);
}
// 0 outside, 1 boundary, 2 inside. Division-free positive-X ray parity.
inline int location(const Loop& loop, const Point& p) {
    bool inside = false;
    for (std::size_t i = 0; i < loop.size(); ++i) {
        const auto& a = loop[i];
        const auto& b = loop[(i + 1) % loop.size()];
        if (on(a, b, p))
            return 1;
        if ((a[1] > p[1]) != (b[1] > p[1]) && ((turn(a, b, p) > 0) == (b[1] > a[1])))
            inside = !inside;
    }
    return inside ? 2 : 0;
}
template <class Region> inline bool filled(const Region& region, const Point& p) {
    return location(region.points, p) == 2 &&
           std::none_of(region.holes.begin(), region.holes.end(),
                        [&](const auto& h) { return location(h, p) != 0; });
}
template <class Region> inline std::vector<const Loop*> loops(const Region& region) {
    std::vector<const Loop*> result{&region.points};
    for (const auto& h : region.holes)
        result.push_back(&h);
    return result;
}
inline bool boundaries_touch(const Loop& a, const Loop& b) {
    for (std::size_t i = 0; i < a.size(); ++i)
        for (std::size_t j = 0; j < b.size(); ++j)
            if (touches(a[i], a[(i + 1) % a.size()], b[j], b[(j + 1) % b.size()]))
                return true;
    return false;
}
// Loops must already be admitted as simple, counterclockwise polygons.
template <class Region> inline void validate_hole(const Region& region, std::size_t i) {
    const auto& h = region.holes[i];
    require(!boundaries_touch(region.points, h) &&
                std::all_of(h.begin(), h.end(),
                            [&](auto p) { return location(region.points, p) == 2; }),
            "Hole must be strictly inside its contour");
    for (std::size_t j = 0; j < i; ++j)
        require(!boundaries_touch(h, region.holes[j]) && location(h, region.holes[j][0]) == 0 &&
                    location(region.holes[j], h[0]) == 0,
                "Holes overlap, nest or touch");
}
inline bool cone(const Point& before, const Point& at, const Point& after, const Point& q) {
    const auto a = turn(before, at, q), b = turn(at, after, q), c = turn(before, at, after);
    if (a == 0 || b == 0 || c == 0)
        return false;
    return c > 0 ? a > 0 && b > 0 : a > 0 || b > 0;
}
template <class Region>
inline bool visible(const Loop& walk, std::size_t at, const Loop& hole, std::size_t h,
                    const Region& region) {
    const auto& a = walk[at];
    const auto& b = hole[h];
    if (a == b ||
        !cone(walk[(at + walk.size() - 1) % walk.size()], a, walk[(at + 1) % walk.size()], b) ||
        !cone(hole[(h + 1) % hole.size()], b, hole[(h + hole.size() - 1) % hole.size()], a))
        return false;
    const auto clear = [&](const Loop& line) {
        for (std::size_t i = 0; i < line.size(); ++i) {
            const auto& c = line[i];
            const auto& d = line[(i + 1) % line.size()];
            if (!touches(a, b, c, d))
                continue;
            if (c != a && d != a && c != b && d != b)
                return false;
            if (turn(a, b, c) == 0 && turn(a, b, d) == 0)
                return false;
        }
        return true;
    };
    if (!clear(walk))
        return false;
    for (const auto* line : loops(region))
        if (!clear(*line))
            return false;
    return filled(region, {(a[0] + b[0]) * .5, (a[1] + b[1]) * .5});
}
template <class Region> inline std::vector<Triangle> decompose_region(const Region& region) {
    auto walk = region.points;
    std::vector<std::size_t> order(region.holes.size());
    std::iota(order.begin(), order.end(), 0);
    const auto rightmost = [](const Loop& h) {
        return (
            *std::max_element(h.begin(), h.end(), [](auto a, auto b) { return a[0] < b[0]; }))[0];
    };
    std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) {
        return rightmost(region.holes[a]) > rightmost(region.holes[b]);
    });
    for (auto key : order) {
        const auto& hole = region.holes[key];
        double best = std::numeric_limits<double>::infinity();
        std::size_t at = 0, h = 0;
        for (std::size_t j = 0; j < hole.size(); ++j)
            for (std::size_t i = 0; i < walk.size(); ++i) {
                const auto x = walk[i][0] - hole[j][0], y = walk[i][1] - hole[j][1],
                           distance = x * x + y * y;
                if (distance < best && visible(walk, i, hole, j, region)) {
                    best = distance;
                    at = i;
                    h = j;
                }
            }
        require(std::isfinite(best), "No interior noncrossing bridge");
        Loop next;
        next.reserve(walk.size() + hole.size() + 2);
        next.insert(next.end(), walk.begin(), walk.begin() + at + 1);
        for (std::size_t j = 0; j < hole.size(); ++j)
            next.push_back(hole[(h + hole.size() - j) % hole.size()]);
        next.push_back(hole[h]);
        next.push_back(walk[at]);
        next.insert(next.end(), walk.begin() + at + 1, walk.end());
        walk = std::move(next);
    }
    std::vector<Triangle> result;
    while (walk.size() > 3) {
        bool removed = false;
        for (std::size_t i = 0; i < walk.size(); ++i) {
            const auto a = walk[(i + walk.size() - 1) % walk.size()], b = walk[i],
                       c = walk[(i + 1) % walk.size()];
            const auto t = turn(a, b, c);
            if (t == 0 && (a == c || on(a, c, b))) {
                walk.erase(walk.begin() + i);
                removed = true;
                break;
            }
            if (t <= 0)
                continue;
            const bool occupied = std::any_of(walk.begin(), walk.end(), [&](auto p) {
                return p != a && p != b && p != c && turn(a, b, p) >= 0 && turn(b, c, p) >= 0 &&
                       turn(c, a, p) >= 0;
            });
            if (occupied)
                continue;
            result.push_back({a, b, c});
            walk.erase(walk.begin() + i);
            removed = true;
            break;
        }
        require(removed, "No nondegenerate ear in bridged contour");
    }
    if (walk.size() == 3) {
        const auto t = turn(walk[0], walk[1], walk[2]);
        require(t >= 0, "Negative final contour piece");
        if (t > 0)
            result.push_back({walk[0], walk[1], walk[2]});
    }
    return result;
}
} // namespace forge::geometry_detail::contour_detail
