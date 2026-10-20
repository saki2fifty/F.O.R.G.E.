#pragma once
#include "tile_grid.hpp"
#include <forge/identity.hpp>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace forge {
// Internal first source profile, not yet registered as an importable asset.
inline constexpr std::size_t tile_map_source_byte_limit = 64 * 1024 * 1024;
inline constexpr std::size_t tile_map_layer_limit = 8, tile_map_chunk_limit = 1024;
inline constexpr std::size_t tile_map_cell_limit = 262144, tile_map_edit_limit = 4096;
struct TileMapLayer {
    std::uint64_t id = 0;
    std::string name;
    bool visible = true;
    std::int32_t order = 0;
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
class TileMapValue {
  public:
    TileMapValue() = default;
    static TileMapValue create(AssetId, AssetId tile_set);
    static TileMapValue parse(nlohmann::json);
    AssetId asset() const;
    AssetId tile_set() const;
    std::uint64_t next_id() const;
    std::array<double, 2> cell_size() const;
    const std::vector<TileMapLayer>& layers() const;
    std::size_t occupied() const;
    std::size_t chunk_count() const;
    const nlohmann::json& metadata() const;
    TileMapCell cell(std::uint64_t layer, TileCellAddress) const;
    // Detached candidate; ordered writes to one address use the last value.
    // Painting an existing cell preserves its unrecognized fields. Erase removes it.
    TileMapValue paint(std::span<const TileMapCellEdit>) const;
    TileMapValue with_metadata(nlohmann::json) const;
    void validate() const;
    std::string dump(int indent = -1) const;
    static std::size_t history_weight(const TileMapValue&, const TileMapValue&);
    friend bool operator==(const TileMapValue&, const TileMapValue&);

  private:
    struct Chunk {
        nlohmann::json document;
        std::array<TileMapCell, tile_chunk_cells> cells{};
        std::size_t occupied = 0, encoded_bytes = 0;
    };
    struct Metadata {
        nlohmann::json document;
        AssetId asset, tile_set;
        std::uint64_t next_id = 1;
        std::array<double, 2> cell_size{};
        std::vector<TileMapLayer> layers;
        std::size_t encoded_bytes = 0;
    };
    std::shared_ptr<const Metadata> metadata_;
    std::map<TileMapChunkKey, std::shared_ptr<const Chunk>> chunks_;
    std::size_t occupied_ = 0, encoded_bytes_ = 0;
    static std::shared_ptr<const Metadata> admit_metadata(nlohmann::json);
    static std::shared_ptr<const Chunk> admit_chunk(nlohmann::json);
    bool has_layer(std::uint64_t) const;
};
struct TileMapSource {
    TileMapValue document;
    static TileMapSource parse(std::span<const std::byte>);
    AssetId asset() const { return document.asset(); }
    void validate() const { document.validate(); }
};
} // namespace forge
