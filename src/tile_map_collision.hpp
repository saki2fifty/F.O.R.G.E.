#pragma once
#include "tile_map_data.hpp"
#include "tile_set_data.hpp"

namespace forge {
class TileMapVariants;
inline constexpr std::size_t tile_map_collision_rectangle_limit = 4096;
struct TileMapCollisionRectangle {
    TileCellAddress origin;
    std::uint32_t width = 0, height = 0; // Cell counts; each rectangle stays inside one chunk.
    bool operator==(const TileMapCollisionRectangle&) const = default;
};
// Pure geometry from the existing immutable cells. Every occupied cell in the
// selected layer is solid, independently of image flags and layer visibility.
// No entities, native handles, resource loads or second mutable map are created.
std::vector<TileMapCollisionRectangle>
tile_map_collision_rectangles(const TileMapData&, std::uint64_t layer,
                              std::size_t limit = tile_map_collision_rectangle_limit);
struct TileMapCollisionPolygon {
    TileCellAddress origin;
    std::array<std::array<double, 2>, tile_collision_vertex_limit>
        points{}; // Cell-local XY; follows image flags.
    std::size_t count = 0;
    // Bit i identifies an original source edge from point i to the next point.
    // Decomposition diagonals and hole bridges are never boundary edges.
    std::uint8_t boundary_edges = 0;
    bool operator==(const TileMapCollisionPolygon&) const = default;
};
struct TileMapCollisionGeometry {
    std::vector<TileMapCollisionRectangle> rectangles;
    std::vector<TileMapCollisionPolygon> polygons;
    bool operator==(const TileMapCollisionGeometry&) const = default;
};
// Definitions and cells must belong to the same admitted snapshot. Both shape
// groups share one budget; no-collision definitions leave holes in merged runs.
TileMapCollisionGeometry
tile_map_collision_geometry(const TileMapData&, const TileSetData&, std::uint64_t layer,
                            std::size_t limit = tile_map_collision_rectangle_limit,
                            const TileMapVariants* = nullptr);
} // namespace forge
