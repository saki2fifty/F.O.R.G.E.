#pragma once
#include "tile_map_values.hpp"
#include <forge/identity.hpp>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <variant>
#include <vector>

namespace forge {
class TileSetData;
class TileMapArchive;
class TileMapChunkInput;
struct TileMapChunkData {
    TileMapChunkKey key;
    std::array<TileMapCell, tile_chunk_cells> cells{};
    std::size_t occupied = 0;
};
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
    // Copied presentation can retain sparse records through the same admission
    // and COW path. Ordinary runtime editing keeps its dense changed chunks.
    TileMapData with_compact_cells(std::span<const TileMapCellEdit>, const TileSetData&) const;
    // Repair an unavailable Set without admitting any new tile definitions.
    // Every supplied value must be empty; unchanged chunks remain shared.
    TileMapData with_erased_cells(std::span<const TileMapCellEdit>) const;
    // Same admission without constructing any candidate chunk arrays.
    void validate_cells(std::span<const TileMapCellEdit>, const TileSetData&) const;
    void for_each_chunk(
        const std::function<void(TileMapChunkKey, std::span<const TileMapCell, tile_chunk_cells>,
                                 std::size_t occupied)>&) const;
    // Scoped values also support compact archive-backed chunks. Cell spans are
    // valid only inside the callback; no dense cache is installed by reading.
    void for_each_chunk_values(
        const std::function<void(TileMapChunkKey, std::span<const TileMapCell, tile_chunk_cells>,
                                 std::size_t occupied)>&) const;
    // False means an absent logical chunk, never an unready resident page.
    bool visit_chunk_values(TileMapChunkKey,
                            const std::function<void(std::span<const TileMapCell, tile_chunk_cells>,
                                                     std::size_t)>&) const;
    // Metadata never decodes a chunk. Identity is an opaque borrowed process-local
    // storage token for COW comparisons, not a cell pointer or persistent ID.
    void for_each_chunk_info(const std::function<void(TileMapChunkKey, std::size_t occupied,
                                                      const void* identity)>&) const;
    TileMapChunkInput chunk_input(TileMapChunkKey) const;
    void validate() const;
    // Retained allocation estimate; includes capacity and conservative node/control
    // storage, without claiming knowledge of allocator headers or process RSS.
    std::size_t resident_bytes() const;
    // Full edited checkpoint projections rebuild dense chunks independently.
    // Charge that representation rather than the compact/shared live backing.
    std::size_t dense_reconstruction_bytes() const;
    // Conservative retained estimate for copied views: each distinct value's
    // index/metadata, and each immutable shared chunk array exactly once.
    static std::size_t resident_bytes(std::span<const TileMapData* const>);

  private:
    struct Chunk {
        std::array<TileMapCell, tile_chunk_cells> cells{};
        std::size_t occupied = 0;
    };
    struct EncodedChunk {
        std::shared_ptr<const std::vector<std::byte>> bytes;
        std::size_t offset = 0, occupied = 0;
        std::span<const std::byte> records() const {
            return std::span<const std::byte>(*bytes).subspan(offset, occupied * 16);
        }
    };
    using ChunkStorage =
        std::variant<std::shared_ptr<const Chunk>, std::shared_ptr<const EncodedChunk>>;
    static std::size_t chunk_occupied(const ChunkStorage&);
    static TileMapCell chunk_cell(const ChunkStorage&, std::size_t);
    static std::array<TileMapCell, tile_chunk_cells> chunk_cells(const ChunkStorage&);
    static void visit_chunk_storage(
        const ChunkStorage&,
        const std::function<void(std::span<const TileMapCell, tile_chunk_cells>, std::size_t)>&);
    struct CellPatch {
        std::map<std::size_t, TileMapCell> cells;
        std::size_t occupied = 0;
    };
    struct CellChanges {
        std::map<TileMapChunkKey, CellPatch> chunks;
        std::size_t occupied = 0;
    };
    CellChanges prepare_cells(std::span<const TileMapCellEdit>, const TileSetData*) const;
    TileMapData apply_cells(const CellChanges&, bool compact = false) const;
    AssetId asset_, tile_set_;
    std::array<double, 2> cell_size_{};
    std::vector<TileMapLayer> layers_;
    std::map<TileMapChunkKey, ChunkStorage> chunks_;
    std::size_t occupied_ = 0;
    bool admitted_ = false;
    bool has_layer(std::uint64_t) const;
    friend class TileMapArchive;
    friend class TileMapChunkInput;
    friend TileMapData decode_cooked_tile_map(std::span<const std::byte>, AssetId);
};
// Cheap immutable worker input. It owns one chunk and its sparse backing, never
// a resource lease, world, filesystem path or revocable scope pointer.
class TileMapChunkInput {
  public:
    AssetId asset() const;
    TileMapChunkKey key() const;
    std::size_t occupied() const;
    const void* identity() const;
    const void* backing_identity() const;
    std::size_t backing_bytes() const;
    std::size_t input_bytes() const;
    std::optional<std::span<const std::byte>> encoded_cell_records() const;
    TileMapChunkData decode(std::stop_token = {}) const;

  private:
    friend class TileMapData;
    friend class TileMapArchive;
    AssetId asset_;
    TileMapChunkKey key_;
    TileMapData::ChunkStorage storage_;
    void validate() const;
};
TileMapData decode_cooked_tile_map(std::span<const std::byte>, AssetId expected);
} // namespace forge
