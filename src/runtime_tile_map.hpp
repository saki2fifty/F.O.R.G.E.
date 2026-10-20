#pragma once
#include "tile_asset_resources.hpp"
#include <forge/world.hpp>

namespace forge::detail {
// Engine-only immutable view of one realized instance, not an SDK pointer API.
struct RuntimeTileMapSnapshot {
    TileMapData cells;
    ResourceLease<TileMapAsset> asset;
    ResourceLease<TileSetAsset> definitions;
    std::uint64_t generation = 0, revision = 0;
    bool edited = false;
};
struct RuntimeTileMapStatus {
    AssetId requested, adopted, tile_set;
    std::string requested_revision, adopted_revision, diagnostic;
    std::uint64_t generation = 0, revision = 0;
    bool ready = false, current = false, edited = false;
};
enum class RuntimeTileEditState { Pending, Applied, Failed };
struct RuntimeTileEditResult {
    RuntimeTileEditState state = RuntimeTileEditState::Pending;
    std::uint64_t generation = 0, revision = 0;
    std::string diagnostic;
};
void register_runtime_tile_maps(WorldContext&);
// Asset adoption may progress while paused; this never publishes cell requests.
void synchronize_runtime_tile_maps(WorldContext&);
RuntimeTileMapStatus inspect_runtime_tile_map(WorldContext&, EntityRef);
std::shared_ptr<const RuntimeTileMapSnapshot> runtime_tile_map_snapshot(WorldContext&, EntityRef);
std::uint64_t request_runtime_tile_cells(WorldContext&, const std::string& module, EntityRef,
                                         std::uint64_t generation,
                                         std::span<const TileMapCellEdit>);
std::uint64_t request_runtime_tile_reset(WorldContext&, const std::string& module, EntityRef,
                                         std::uint64_t generation);
RuntimeTileEditResult inspect_runtime_tile_edit(WorldContext&, const std::string& module,
                                                std::uint64_t token);
bool release_runtime_tile_edit(WorldContext&, const std::string& module, std::uint64_t token);
void publish_runtime_tile_edits(WorldContext&);
} // namespace forge::detail
