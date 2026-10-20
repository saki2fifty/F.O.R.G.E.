#pragma once
#include "tile_map_values.hpp"
#include <forge/identity.hpp>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <vector>

namespace forge {
class TileSetData;
inline constexpr std::array<std::byte, 8> tile_map_cooked_magic{
    std::byte{'F'}, std::byte{'T'}, std::byte{'I'}, std::byte{'L'},
    std::byte{'E'}, std::byte{'M'}, std::byte{'A'}, std::byte{'P'}};
inline constexpr std::size_t tile_map_cooked_metadata_limit = 65536;
inline constexpr std::size_t tile_map_cooked_payload_limit =
    tile_map_chunk_limit * 20 + tile_map_cell_limit * 16;
// Pure cooked runtime values. No authoring JSON, document/history, file access,
// compiler, graphics device or dependency selection is retained here.
class TileMapData {
  public:
    AssetId asset() const;
    AssetId tile_set() const;
    std::array<double, 2> cell_size() const;
    const std::vector<TileMapLayer>& layers() const;
    std::size_t occupied() const;
    std::size_t chunk_count() const;
    TileMapCell cell(std::uint64_t layer, TileCellAddress) const;
    // Detached native candidate: ordered writes, last value wins at one address.
    // Validate written cells against this map's admitted definitions before changing
    // any snapshot. The eventual world owner controls fixed-phase publication.
    TileMapData with_cells(std::span<const TileMapCellEdit>, const TileSetData&) const;
    // Repair an unavailable Set without admitting any new tile definitions.
    // Every supplied value must be empty; unchanged chunks remain shared.
    TileMapData with_erased_cells(std::span<const TileMapCellEdit>) const;
    // Same admission without constructing any candidate chunk arrays.
    void validate_cells(std::span<const TileMapCellEdit>, const TileSetData&) const;
    void for_each_chunk(
        const std::function<void(TileMapChunkKey, std::span<const TileMapCell, tile_chunk_cells>,
                                 std::size_t occupied)>&) const;
    void validate() const;
    // Retained allocation estimate; includes capacity and conservative node/control
    // storage, without claiming knowledge of allocator headers or process RSS.
    std::size_t resident_bytes() const;

  private:
    struct Chunk {
        std::array<TileMapCell, tile_chunk_cells> cells{};
        std::size_t occupied = 0;
    };
    struct CellPatch {
        std::map<std::size_t, TileMapCell> cells;
        std::size_t occupied = 0;
    };
    struct CellChanges {
        std::map<TileMapChunkKey, CellPatch> chunks;
        std::size_t occupied = 0;
    };
    CellChanges prepare_cells(std::span<const TileMapCellEdit>, const TileSetData*) const;
    TileMapData apply_cells(const CellChanges&) const;
    AssetId asset_, tile_set_;
    std::array<double, 2> cell_size_{};
    std::vector<TileMapLayer> layers_;
    std::map<TileMapChunkKey, std::shared_ptr<const Chunk>> chunks_;
    std::size_t occupied_ = 0;
    bool admitted_ = false;
    bool has_layer(std::uint64_t) const;
    friend TileMapData decode_cooked_tile_map(std::span<const std::byte>, AssetId);
};
TileMapData decode_cooked_tile_map(std::span<const std::byte>, AssetId expected);
} // namespace forge
