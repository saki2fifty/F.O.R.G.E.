#pragma once
#include "tile_polygon_decomposition.hpp"
#include <limits>
#include <map>

namespace forge::tile_detail::contour_detail {
using Point = forge::tile_detail::TilePolygonPoint;
using Loop = std::vector<Point>;
using Triangle = std::array<Point, 3>;
using Region = TileCollisionContour;
inline double turn(const Point& a, const Point& b, const Point& c) {
    return forge::tile_detail::tile_polygon_turn(a, b, c);
}
inline void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
inline bool on(const Point& a, const Point& b, const Point& p) {
    return tile_polygon_on_segment(a, b, p);
}
inline bool touches(const Point& a, const Point& b, const Point& c, const Point& d) {
    return tile_polygon_segments_touch(a, b, c, d);
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
inline bool filled(const Region& region, const Point& p) {
    return location(region.points, p) == 2 &&
           std::none_of(region.holes.begin(), region.holes.end(),
                        [&](const auto& h) { return location(h, p) != 0; });
}
inline std::vector<const Loop*> loops(const Region& region) {
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
inline void validate(std::span<const Region> regions) {
    // Source topology admission; native float conversion remains a later boundary.
    require(!regions.empty() && regions.size() <= tile_collision_contour_limit,
            "Need1..4 filled contours");
    for (const auto& r : regions) {
        (void)forge::tile_detail::tile_polygon_triangles(r.points);
        require(r.holes.size() <= tile_collision_hole_limit, "Too many holes");
        for (std::size_t i = 0; i < r.holes.size(); ++i) {
            const auto& h = r.holes[i];
            (void)forge::tile_detail::tile_polygon_triangles(h);
            require(!boundaries_touch(r.points, h) &&
                        std::all_of(h.begin(), h.end(),
                                    [&](auto p) { return location(r.points, p) == 2; }),
                    "Hole must be strictly inside its contour");
            for (std::size_t j = 0; j < i; ++j)
                require(!boundaries_touch(h, r.holes[j]) && location(h, r.holes[j][0]) == 0 &&
                            location(r.holes[j], h[0]) == 0,
                        "Holes overlap, nest or touch");
        }
    }
    for (std::size_t i = 0; i < regions.size(); ++i)
        for (std::size_t j = 0; j < i; ++j) {
            for (const auto* a : loops(regions[i]))
                for (const auto* b : loops(regions[j]))
                    require(!boundaries_touch(*a, *b), "Contour boundaries touch or cross");
            require(!filled(regions[i], regions[j].points[0]) &&
                        !filled(regions[j], regions[i].points[0]),
                    "Filled contours overlap");
        }
}
inline bool cone(const Point& before, const Point& at, const Point& after, const Point& q) {
    const auto a = turn(before, at, q), b = turn(at, after, q), c = turn(before, at, after);
    if (a == 0 || b == 0 || c == 0)
        return false;
    return c > 0 ? a > 0 && b > 0 : a > 0 || b > 0;
}
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
inline std::vector<Triangle> decompose_region(const Region& region) {
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
inline std::vector<Triangle> decompose(std::span<const Region> regions) {
    validate(regions);
    std::vector<Triangle> out;
    for (const auto& r : regions) {
        auto triangles = decompose_region(r);
        out.insert(out.end(), triangles.begin(), triangles.end());
    }
    return out;
}
} // namespace forge::tile_detail::contour_detail

namespace forge::tile_detail {
inline TileCollisionContours
prepare_tile_collision_contours(std::vector<TileCollisionContour> contours) {
    const auto pieces = contour_detail::decompose(contours);
    TileCollisionContours result;
    result.contours = std::move(contours);
    std::map<TilePolygonPoint, std::uint16_t> indices;
    const auto append = [&](const auto& points) {
        for (const auto& point : points) {
            contour_detail::require(result.vertices.size() < tile_collision_contour_vertex_limit,
                                    "Tile contours exceed the source vertex profile");
            const auto index = std::uint16_t(result.vertices.size());
            contour_detail::require(indices.emplace(point, index).second,
                                    "Tile contours share an ambiguous vertex");
            result.vertices.push_back(point);
        }
    };
    for (const auto& region : result.contours) {
        append(region.points);
        for (const auto& hole : region.holes)
            append(hole);
    }
    result.triangles.reserve(pieces.size());
    for (const auto& piece : pieces)
        result.triangles.push_back(
            {indices.at(piece[0]), indices.at(piece[1]), indices.at(piece[2])});
    return result;
}
} // namespace forge::tile_detail
