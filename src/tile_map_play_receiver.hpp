#pragma once
#include "tile_map_play_projection.hpp"
#include <set>

namespace forge {
struct RenderScene;
// Derived receiving state, composed into the existing PlaySession owner. The
// caller sends one reserved request through its correlated transport, then feeds
// its payload back to receive(). This adapter owns no IO or simulation state.
class TileMapPlayReceiver {
  public:
    void observe(const nlohmann::json& manifest);
    std::optional<nlohmann::json> next_request();
    bool receive(const nlohmann::json& payload);
    void bind(RenderScene&) const;
    void clear();
    bool pending() const;
    std::size_t retained_bytes() const;
    const std::string& diagnostic() const { return diagnostic_; }
    std::uint64_t world() const { return world_; }
    AssetId scene() const { return scene_; }

  private:
    struct Wanted {
        TileMapPlayView value;
        AssetId definitions;
        std::size_t cells = 0, chunks = 0;
        bool ready = false;
    };
    enum class Operation { Begin, Page, Acknowledge, Cancel };
    struct Flight {
        Operation operation;
        Wanted wanted;
        TileMapPlayView base;
        std::uint64_t token = 0;
        std::size_t cursor = 0;
    };
    struct Transfer {
        TileMapPlayView value;
        std::uint64_t token = 0;
        std::size_t cursor = 0, total = 0, cells = 0, chunks = 0;
        std::set<TileMapChunkKey> seen;
        std::set<TileMapChunkKey> expected;
        bool full = false;
    };
    std::thread::id owner_ = std::this_thread::get_id();
    std::uint64_t world_ = 0;
    AssetId scene_;
    EntityId last_actor_;
    std::map<EntityId, Wanted> wanted_;
    std::map<EntityId, TileMapPlayView> frames_;
    std::optional<Flight> flight_;
    std::optional<Transfer> transfer_;
    std::uint64_t cancel_ = 0;
    bool defer_ = false;
    std::string diagnostic_;
    void owner() const;
    bool compatible(const TileMapPlayView&) const;
    void complete() const;
    std::size_t cache_bytes(const TileMapPlayView* replacement = nullptr) const;
};
} // namespace forge
