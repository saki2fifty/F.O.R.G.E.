#pragma once
#include "tile_map_projection.hpp"
#include <optional>
#include <thread>

namespace forge {
// Private values supplied by the runtime transport owner. These are detached
// presentation observations, never a second simulation or authored map.
struct TileMapPlayView {
    EntityId entity;
    AssetId requested;
    std::uint64_t generation = 0, revision = 0;
    bool pending = false, edited = false;
    std::string map_revision, set_revision, diagnostic;
    std::shared_ptr<const TileMapData> cells;
    std::shared_ptr<const TileSetData> definitions;
};
// Composed into an existing runtime transport owner, one per world scope. No IO,
// thread, world, gameplay commands or catalog is owned by this adapter.
class TileMapPlaySource {
  public:
    nlohmann::json capture(std::uint64_t world, AssetId scene, std::span<const TileMapPlayView>);
    nlohmann::json begin(EntityId, std::uint64_t generation, std::uint64_t revision,
                         std::uint64_t base_generation, std::uint64_t base_revision);
    nlohmann::json page(std::uint64_t token, std::size_t cursor);
    bool acknowledge(std::uint64_t token);
    bool cancel(std::uint64_t token);
    void clear();
    std::uint64_t world() const { return world_; }
    AssetId scene() const { return scene_; }
    // Retained copied values only. Observations borrow the runtime's existing
    // resource-accounted snapshots; this is not a process RSS estimate.
    std::size_t retained_bytes() const;

  private:
    struct Value {
        TileMapData cells;
        TileSetData definitions;
        std::uint64_t generation = 0, revision = 0;
        std::string map_revision, set_revision;
    };
    struct Baseline {
        std::shared_ptr<const Value> value;
        std::uint64_t age = 0;
    };
    struct Transfer {
        EntityId entity;
        std::shared_ptr<const Value> value;
        std::vector<TileMapChunkKey> chunks;
        std::uint64_t token = 0;
        std::size_t cursor = 0;
    };
    std::thread::id owner_ = std::this_thread::get_id();
    std::uint64_t world_ = 0, sequence_ = 0, last_ack_ = 0;
    AssetId scene_;
    std::map<EntityId, TileMapPlayView> observed_;
    std::map<EntityId, Baseline> baselines_;
    std::optional<Transfer> transfer_;
    void owner() const;
    void trim();
};
} // namespace forge
