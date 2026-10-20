#pragma once
#include "tile_grid.hpp"
#include <string>

namespace forge {
inline constexpr std::size_t tile_map_layer_limit = 8, tile_map_chunk_limit = 1024;
inline constexpr std::size_t tile_map_cell_limit = 262144, tile_map_edit_limit = 4096;
struct TileMapLayer {
    std::uint64_t id = 0;
    std::string name;
    bool visible = true;
    std::int32_t order = 0;
    bool operator==(const TileMapLayer&) const = default;
};
struct TileMapCell {
    std::uint64_t tile = 0; // Zero is empty.
    std::uint32_t flags = 0;
    bool operator==(const TileMapCell&) const = default;
};
struct TileMapCellEdit {
    std::uint64_t layer = 0;
    TileCellAddress address;
    TileMapCell value;
};
struct TileMapChunkKey {
    std::uint64_t layer = 0;
    TileChunkAddress address;
    auto operator<=>(const TileMapChunkKey&) const = default;
};
} // namespace forge
