#pragma once
#include "navigation_layers.hpp"
#include <forge/identity.hpp>
#include <memory>

class dtNavMesh;
namespace forge::navigation_detail {
struct CylinderObstacle {
    EntityId entity;
    std::array<float, 3> position{}; // Native bottom center, already agent-inflated.
    float radius = 0, height = 0;
    bool operator==(const CylinderObstacle&) const = default;
};
// A private query derivative, never shared between Worlds or persisted.
class TileCache {
  public:
    explicit TileCache(const HeightfieldLayers&);
    ~TileCache();
    dtNavMesh* mesh() const;
    bool update(std::span<const CylinderObstacle>);
    std::uint64_t rebuild_count() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace forge::navigation_detail
