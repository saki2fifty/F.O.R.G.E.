#include "tile_map_collision.hpp"
#include "tile_map_variants.hpp"
#include "tile_polygon_decomposition.hpp"
#include <algorithm>
#include <map>

namespace forge {
namespace {
template <class Map, class Solid>
std::vector<TileMapCollisionRectangle> rectangles(const Map& map, std::uint64_t layer,
                                                  std::size_t limit, Solid&& solid) {
    map.validate();
    if (!limit || limit > tile_map_collision_rectangle_limit)
        throw std::invalid_argument("Tile Map collision rectangle budget must be1..4096");
    if (!layer || std::none_of(map.layers().begin(), map.layers().end(),
                               [&](const auto& row) { return row.id == layer; }))
        throw std::invalid_argument("Tile Map collision selects an unavailable layer");
    std::vector<TileMapCollisionRectangle> result;
    map.for_each_chunk([&](auto key, auto cells, auto) {
        if (key.layer != layer)
            return;
        using Run = std::pair<std::uint32_t, std::uint32_t>;
        std::map<Run, std::size_t> previous;
        for (std::uint32_t y = 0; y < tile_chunk_side; ++y) {
            std::map<Run, std::size_t> current;
            for (std::uint32_t x = 0; x < tile_chunk_side;) {
                if (!solid(cells[y * tile_chunk_side + x])) {
                    ++x;
                    continue;
                }
                const auto start = x++;
                while (x < tile_chunk_side && solid(cells[y * tile_chunk_side + x]))
                    ++x;
                const Run run{start, x - start};
                const auto old = previous.find(run);
                if (old != previous.end()) {
                    ++result[old->second].height;
                    current.emplace(run, old->second);
                } else {
                    if (result.size() == limit)
                        throw std::runtime_error("Tile Map collision rectangle capacity exceeded");
                    current.emplace(run, result.size());
                    result.push_back({tile_cell_address({key.address, y * tile_chunk_side + start}),
                                      run.second, 1});
                }
            }
            previous = std::move(current);
        }
    });
    return result;
}
} // namespace
std::vector<TileMapCollisionRectangle>
tile_map_collision_rectangles(const TileMapData& map, std::uint64_t layer, std::size_t limit) {
    return rectangles(map, layer, limit, [](const auto& cell) { return bool(cell.tile); });
}
namespace {
template <class Map>
TileMapCollisionGeometry collision_geometry(const Map& map, const TileSetData& definitions,
                                            std::uint64_t layer, std::size_t limit) {
    if (definitions.asset() != map.tile_set())
        throw std::invalid_argument("Tile collision definitions belong to a different Tile Set");
    TileMapCollisionGeometry result;
    // Legacy definitions keep the original occupied-cell projection and ordering.
    const bool full =
        std::all_of(definitions.tiles().begin(), definitions.tiles().end(), [](const auto& tile) {
            return tile.collision.kind == TileCollisionKind::FullCell;
        });
    if (full) {
        map.for_each_chunk([&](auto key, auto cells, auto) {
            if (key.layer == layer)
                for (const auto& cell : cells)
                    if (cell.tile)
                        (void)definitions.tile(cell.tile);
        });
        result.rectangles =
            rectangles(map, layer, limit, [](const auto& cell) { return bool(cell.tile); });
        return result;
    }
    result.rectangles = rectangles(map, layer, limit, [&](const auto& cell) {
        return cell.tile &&
               definitions.tile(cell.tile).collision.kind == TileCollisionKind::FullCell;
    });
    map.for_each_chunk([&](auto key, auto cells, auto) {
        if (key.layer != layer)
            return;
        for (std::uint32_t slot = 0; slot < cells.size(); ++slot) {
            const auto& cell = cells[slot];
            if (!cell.tile)
                continue;
            const auto& profile = definitions.tile(cell.tile).collision;
            if (profile.kind != TileCollisionKind::Convex &&
                profile.kind != TileCollisionKind::Polygon)
                continue;
            TileMapCollisionPolygon polygon;
            polygon.origin = tile_cell_address({key.address, slot});
            polygon.count = profile.points.size();
            // Invert the ordinary Sprite atlas mapping: source XY is up, UV rows
            // are down, and the draw transposes logical UV before atlas flips.
            const auto image = sprite_region_mapping({0, 0, 1, 1}, 1, 1, bool(cell.flags & 1),
                                                     bool(cell.flags & 2));
            for (std::size_t i = 0; i < polygon.count; ++i) {
                std::array<double, 2> uv{profile.points[i][0], 1 - profile.points[i][1]};
                for (unsigned axis = 0; axis < 2; ++axis)
                    uv[axis] = (uv[axis] - image.origin[axis]) / image.scale[axis];
                if (cell.flags & 4)
                    std::swap(uv[0], uv[1]);
                polygon.points[i] = {uv[0], 1 - uv[1]};
            }
            const auto append = [&](const TileMapCollisionPolygon& value) {
                if (result.rectangles.size() + result.polygons.size() == limit)
                    throw std::runtime_error("Tile Map collision shape capacity exceeded");
                result.polygons.push_back(value);
            };
            if (profile.kind == TileCollisionKind::Convex) {
                append(polygon);
            } else {
                for (const auto& indices : tile_detail::tile_polygon_triangles(profile.points)) {
                    TileMapCollisionPolygon piece;
                    piece.origin = polygon.origin;
                    piece.count = indices.size();
                    for (std::size_t i = 0; i < indices.size(); ++i)
                        piece.points[i] = polygon.points[indices[i]];
                    append(piece);
                }
            }
        }
    });
    return result;
}
} // namespace
TileMapCollisionGeometry tile_map_collision_geometry(const TileMapData& map,
                                                     const TileSetData& definitions,
                                                     std::uint64_t layer, std::size_t limit,
                                                     const TileMapVariants* supplied) {
    const bool full =
        std::all_of(definitions.tiles().begin(), definitions.tiles().end(), [](const auto& tile) {
            return tile.collision.kind == TileCollisionKind::FullCell;
        });
    if (!definitions.has_autotiles() || full)
        return collision_geometry(map, definitions, layer, limit);
    if (supplied && supplied->matches(&map, &definitions))
        return collision_geometry(*supplied, definitions, layer, limit);
    const auto variants = resolve_tile_variants(std::make_shared<const TileMapData>(map),
                                                std::make_shared<const TileSetData>(definitions));
    return collision_geometry(*variants, definitions, layer, limit);
}
} // namespace forge
