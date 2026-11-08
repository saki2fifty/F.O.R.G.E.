#pragma once
#include "polygon_contours.hpp"
#include "tile_polygon_decomposition.hpp"
#include <map>

namespace forge::tile_detail::contour_detail {
using geometry_detail::contour_detail::boundaries_touch;
using geometry_detail::contour_detail::cone;
using geometry_detail::contour_detail::decompose_region;
using geometry_detail::contour_detail::filled;
using geometry_detail::contour_detail::location;
using geometry_detail::contour_detail::Loop;
using geometry_detail::contour_detail::loops;
using geometry_detail::contour_detail::on;
using geometry_detail::contour_detail::Point;
using geometry_detail::contour_detail::require;
using geometry_detail::contour_detail::touches;
using geometry_detail::contour_detail::turn;
using geometry_detail::contour_detail::validate_hole;
using geometry_detail::contour_detail::visible;
using Triangle = geometry_detail::contour_detail::Triangle;
using Region = TileCollisionContour;
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
            validate_hole(r, i);
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
