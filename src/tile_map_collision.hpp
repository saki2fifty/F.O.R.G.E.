#pragma once
#include "tile_map_data.hpp"

namespace forge {
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
} // namespace forge
