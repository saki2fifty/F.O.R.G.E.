#pragma once
#include "navigation_collision_source.hpp"
#include "navigation_layers.hpp"
#include "navigation_region.hpp"
#include "navigation_tile_geometry.hpp"
#include <forge/geometry.hpp>
#include <forge/mesh_resource.hpp>
#include <forge/navigation_build.hpp>
#include <functional>
#include <optional>
#include <set>
namespace forge::navigation_detail {
inline constexpr std::size_t max_mesh_inputs = 32, max_tile_inputs = 32;
inline constexpr std::uint64_t mesh_bytes = 64 * 1024 * 1024;
inline constexpr MeshLimits mesh_limits{32 * 1024 * 1024, 65536, 49152, 512, 64, 0, 16};
struct Surface {
    EntityId entity;
    AffineTransform transform;
    unsigned primitive = no_primitive;
    AssetId mesh;
    std::optional<NavigationRegion2D> region;
    std::optional<NavigationColliderSource> collider;
};
struct Sources {
    AssetId scene;
    nlohmann::json identities = nlohmann::json::array();
    std::vector<Surface> surfaces;
    std::set<AssetId> meshes, maps;
    NavigationPlane plane = NavigationPlane::Spatial3D;
};
inline bool collider_sources(const Sources& value) {
    return std::any_of(value.surfaces.begin(), value.surfaces.end(),
                       [](const auto& surface) { return bool(surface.collider); });
}
Sources capture(const nlohmann::json& effective_scene,
                NavigationPlane = NavigationPlane::Spatial3D);
nlohmann::json mesh_inputs(const Sources&, const std::function<std::string(AssetId)>& revision);
nlohmann::json tile_inputs(const Sources&, const std::function<std::string(AssetId)>& revision);
std::string source_digest(const Sources&, const nlohmann::json& mesh_inputs);
struct Geometry {
    std::vector<float> vertices;
    std::vector<int> indices;
    nlohmann::json sources = nlohmann::json::array();
    AssetId scene;
    std::string digest;
    // Bounded convex native-XZ masks; original rectangles retain their four vertices.
    std::vector<std::vector<float>> obstacles;
};
Geometry geometry(const nlohmann::json& effective_scene);
Geometry geometry(const Sources&); // Original primitive-only v1 canonical bytes.
Geometry geometry(const Sources&, const nlohmann::json& resource_inputs,
                  const std::function<const MeshResourceData&(AssetId)>& mesh,
                  const std::function<RegionContours(const Surface&)>& maps = {});
HeightfieldLayers build_layers(const Geometry&, NavigationSettings);
std::vector<std::byte> build_tile(const Geometry&, NavigationSettings);
} // namespace forge::navigation_detail
