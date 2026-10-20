#pragma once
#include "tile_map_data.hpp"
#include "tile_set_data.hpp"

namespace forge {
// Pure copied presentation codec for the existing Play transport. No world,
// history, asset selection, networking or active presentation is owned here.
inline constexpr std::size_t tile_projection_page_chunks = 4;
inline constexpr std::size_t tile_projection_page_bytes = 256 * 1024;
struct TileMapProjectionHeader {
    TileMapData empty;
    TileSetData definitions;
    std::size_t expected_cells = 0, expected_chunks = 0;
};
nlohmann::json tile_map_projection_header(const TileMapData&, const TileSetData&);
TileMapProjectionHeader decode_tile_map_projection_header(const nlohmann::json&, AssetId map,
                                                          AssetId definitions);
// Both snapshots must have the same identity/layout. Removed or shrinking
// chunks precede growing chunks so paging an atomic move cannot exceed capacity.
std::vector<TileMapChunkKey> tile_map_projection_changes(const TileMapData&,
                                                         const TileMapData* previous = nullptr);
nlohmann::json tile_map_projection_page(const TileMapData&, std::span<const TileMapChunkKey>);
TileMapData apply_tile_map_projection_page(const TileMapData&, const TileSetData&,
                                           const nlohmann::json&);
} // namespace forge
