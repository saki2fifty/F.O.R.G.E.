#pragma once
#include "tile_map_data.hpp"
#include "tile_set_data.hpp"

namespace forge {
struct TileVariantOverride {
    std::uint16_t slot = 0;
    std::uint64_t tile = 0;
    bool operator==(const TileVariantOverride&) const = default;
};
// Immutable derived selection. Logical source/runtime/SDK cells remain unchanged.
// Original identities are weak: a containing runtime Snapshot can own this value
// without a strong alias back to itself. Copied raw chunks/rows retain read safety.
class TileMapVariants {
  public:
    using Overrides = std::vector<TileVariantOverride>;
    bool matches(const TileMapData*, const TileSetData*) const;
    const TileMapData& source() const { return source_; }
    const TileSetData& definitions() const { return definitions_; }
    void validate() const {
        source_.validate();
        definitions_.validate();
    }
    const std::vector<TileMapLayer>& layers() const { return source_.layers(); }
    AssetId tile_set() const { return source_.tile_set(); }
    std::span<const TileVariantOverride> overrides(TileMapChunkKey) const;
    // Selected spans are borrowed only for this callback; never retain them.
    void for_each_chunk(
        const std::function<void(TileMapChunkKey, std::span<const TileMapCell, tile_chunk_cells>,
                                 std::size_t occupied)>&) const;
    void for_each_chunk_values(
        const std::function<void(TileMapChunkKey, std::span<const TileMapCell, tile_chunk_cells>,
                                 std::size_t occupied)>& visit) const {
        for_each_chunk(visit);
    }
    std::size_t recomputed_chunks() const { return recomputed_chunks_; }
    // Extra retained allocation estimate beyond the live original source values.
    // Distinct index copies count; shared source arrays/override vectors count once.
    static std::size_t resident_bytes(std::span<const TileMapVariants* const>);

  private:
    std::weak_ptr<const TileMapData> original_source_;
    std::weak_ptr<const TileSetData> original_definitions_;
    TileMapData source_;
    TileSetData definitions_;
    std::map<TileMapChunkKey, std::shared_ptr<const Overrides>> chunks_;
    std::size_t recomputed_chunks_ = 0;
    friend std::shared_ptr<const TileMapVariants>
        resolve_tile_variants(std::shared_ptr<const TileMapData>,
                              std::shared_ptr<const TileSetData>,
                              std::shared_ptr<const TileMapVariants>);
};
std::shared_ptr<const TileMapVariants>
resolve_tile_variants(std::shared_ptr<const TileMapData>, std::shared_ptr<const TileSetData>,
                      std::shared_ptr<const TileMapVariants> previous = {});
} // namespace forge
