#pragma once
#include "navigation_space.hpp"
#include "navigation_tile_cache.hpp"
#include <forge/navigation_service.hpp>
#include <memory>
#include <span>
namespace forge::navigation_detail {
inline constexpr const char* recast_revision = "6dc1667f580357e8a2154c28b7867bea7e8ad3a7";
inline constexpr std::size_t max_nav_bytes = 4 * 1024 * 1024;
inline constexpr unsigned max_polygons = 4096, max_waypoints = 64;
nlohmann::json tile_properties(std::span<const std::byte> bytes);
void validate_tile(std::span<const std::byte> bytes, MeshSpace = MeshSpace::Spatial3D);
class Mesh {
  public:
    explicit Mesh(std::span<const std::byte> bytes, MeshSpace = MeshSpace::Spatial3D,
                  bool dynamic_obstacles = false);
    ~Mesh();
    MeshSpace space() const;
    std::vector<Double3> triangles() const;
    bool dynamic_obstacles() const;

  private:
    friend class Query;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
class Query {
  public:
    struct Neighbour {
        Double3 position, velocity;
    };
    explicit Query(std::shared_ptr<const Mesh> mesh);
    ~Query();
    bool obstacles(std::span<const CylinderObstacle>);
    std::uint64_t generation() const;
    std::uint64_t tile_rebuilds() const;
    NavResult project(Double3 point);
    NavResult path(Double3 start, Double3 end);
    NavResult move(Double3 start, Double3 target);
    // Owned velocity in points[0]; private engine steering, not a public route API.
    NavResult avoid_velocity(Double3 position, Double3 desired, Double3 previous, float speed,
                             float radius, float horizon, std::span<const Neighbour>);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace forge::navigation_detail
