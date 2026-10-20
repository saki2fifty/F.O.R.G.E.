#pragma once
#include "tile_map_values.hpp"
#include <forge/identity.hpp>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace forge {
// Internal first source profile, admitted through the ordinary tile importer.
inline constexpr std::size_t tile_map_source_byte_limit = 64 * 1024 * 1024;
inline constexpr std::size_t tile_map_source_event_limit = 4000000;
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
    // Borrowed immutable cells remain valid while this snapshot is alive.
    // A later paint candidate cannot alter them. This is engine-internal data,
    // not a pointer-returning runtime SDK contract.
    void for_each_chunk(
        const std::function<void(TileMapChunkKey, std::span<const TileMapCell, tile_chunk_cells>,
                                 std::size_t occupied)>&) const;
    TileMapCell cell(std::uint64_t layer, TileCellAddress) const;
    // Detached candidate; ordered writes to one address use the last value.
    // Painting an existing cell preserves its unrecognized fields. Erase removes it.
    TileMapValue paint(std::span<const TileMapCellEdit>) const;
    TileMapValue with_metadata(nlohmann::json) const;
    void validate() const;
    std::string dump(int indent = -1) const;
    static std::size_t history_weight(const TileMapValue&, const TileMapValue&);
    friend bool operator==(const TileMapValue&, const TileMapValue&);
    // The document owner adopts saved values after atomic file publication.
    // Member swaps keep that operation nonthrowing on every supported STL;
    // generic swap may instead invoke an allocating tree move constructor.
    friend void swap(TileMapValue& a, TileMapValue& b) noexcept {
        static_assert(noexcept(a.chunks_.swap(b.chunks_)));
        a.metadata_.swap(b.metadata_);
        a.chunks_.swap(b.chunks_);
        std::swap(a.occupied_, b.occupied_);
        std::swap(a.encoded_bytes_, b.encoded_bytes_);
        std::swap(a.json_events_, b.json_events_);
    }

  private:
    struct Chunk {
        nlohmann::json document;
        std::array<TileMapCell, tile_chunk_cells> cells{};
        std::size_t occupied = 0, encoded_bytes = 0, json_events = 0;
    };
    struct Metadata {
        nlohmann::json document;
        AssetId asset, tile_set;
        std::uint64_t next_id = 1;
        std::array<double, 2> cell_size{};
        std::vector<TileMapLayer> layers;
        std::size_t encoded_bytes = 0, json_events = 0;
    };
    std::shared_ptr<const Metadata> metadata_;
    std::map<TileMapChunkKey, std::shared_ptr<const Chunk>> chunks_;
    std::size_t occupied_ = 0, encoded_bytes_ = 0, json_events_ = 0;
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
