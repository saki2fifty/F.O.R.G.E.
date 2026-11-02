#pragma once
#include "navigation_space.hpp"
#include <array>
#include <forge/identity.hpp>
#include <nlohmann/json.hpp>
#include <span>
#include <vector>

struct dtNavMeshCreateParams;
namespace forge::navigation_detail {
inline constexpr std::size_t navigation_link_limit = 32;
// Derived immutable Nav data. Endpoints are world coordinates; XY depth is zero.
// Scene entities remain the source of logical identity and authored values.
struct NavigationLinkData {
    EntityId entity;
    std::array<float, 3> start{}, end{};
    float radius = .5f;
    bool bidirectional = true;
    bool operator==(const NavigationLinkData&) const = default;
};
void validate_navigation_links(std::span<const NavigationLinkData>, MeshSpace);
nlohmann::json navigation_link_values(std::span<const NavigationLinkData>, MeshSpace);
std::vector<NavigationLinkData> read_navigation_links(const nlohmann::json&, MeshSpace);

// Bounded call-lifetime storage for the dependency's public builder API. The
// caller keeps this alive through dtCreateNavMeshData or the tile-cache callback.
class NativeNavigationLinks {
  public:
    NativeNavigationLinks(std::span<const NavigationLinkData>, MeshSpace, float flat_altitude = 0);
    void apply(dtNavMeshCreateParams&) const;

  private:
    std::size_t count_ = 0;
    std::array<float, navigation_link_limit * 6> vertices_{};
    std::array<float, navigation_link_limit> radii_{};
    std::array<unsigned short, navigation_link_limit> flags_{};
    std::array<unsigned char, navigation_link_limit> areas_{}, directions_{};
    std::array<unsigned int, navigation_link_limit> ids_{};
};
} // namespace forge::navigation_detail
