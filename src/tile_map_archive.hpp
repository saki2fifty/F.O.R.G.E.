#pragma once
#include "tile_map_data.hpp"
#include <stop_token>

namespace forge {
// An entry in the admitted cooked revision, not a persistent asset/subasset ID.
struct TileMapArchiveChunk {
    TileMapChunkKey key;
    std::size_t occupied = 0;

  private:
    friend class TileMapArchive;
    std::size_t offset_ = 0;
};
// Complete logical cells in immutable sparse backing, without allocating every
// dense chunk. No filesystem, resource pool, world or device is retained. Copies
// share owned bytes and index metadata, so decoding remains safe after a cache/source is removed.
class TileMapArchive {
  public:
    AssetId asset() const;
    AssetId tile_set() const;
    std::array<double, 2> cell_size() const;
    const std::vector<TileMapLayer>& layers() const;
    std::span<const TileMapArchiveChunk> chunks() const;
    std::size_t occupied() const;
    TileMapCell cell(std::uint64_t layer, TileCellAddress) const;
    TileMapChunkData decode_chunk(TileMapChunkKey, std::stop_token = {}) const;
    TileMapChunkInput chunk_input(TileMapChunkKey) const;
    TileMapData materialize(std::stop_token = {}) const;
    // Complete logical runtime value backed by sparse chunks. COW edits use the
    // existing TileMapData validator; only touched chunks become dense values.
    TileMapData compact() const;
    void validate() const;
    // Conservative retained estimate, including the complete shared backing.
    // Individual archive copies may count that same backing more than once.
    std::size_t resident_bytes() const;

  private:
    struct Storage {
        AssetId asset, tile_set;
        std::array<double, 2> cell_size{};
        std::vector<TileMapLayer> layers;
        std::vector<TileMapArchiveChunk> chunks;
        std::size_t occupied = 0;
        std::shared_ptr<const std::vector<std::byte>> bytes;
    };
    std::shared_ptr<const Storage> storage_;
    static Storage index(std::span<const std::byte>, AssetId, TileMapData* complete = nullptr);
    const TileMapArchiveChunk* find(TileMapChunkKey) const;
    TileMapChunkInput chunk_input(const TileMapArchiveChunk&) const;
    static TileMapChunkData decode_chunk(std::span<const std::byte>, const TileMapArchiveChunk&,
                                         std::stop_token);
    static TileMapData materialize(const Storage&, std::span<const std::byte>, std::stop_token);
    friend TileMapArchive decode_tile_map_archive(std::span<const std::byte>, AssetId);
    friend TileMapData decode_cooked_tile_map(std::span<const std::byte>, AssetId);
};
TileMapArchive decode_tile_map_archive(std::span<const std::byte>, AssetId expected);
} // namespace forge
