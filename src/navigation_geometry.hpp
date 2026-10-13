#pragma once
#include <forge/geometry.hpp>
#include <forge/mesh_resource.hpp>
#include <forge/navigation_build.hpp>
#include <functional>
#include <set>
namespace forge::navigation_detail {
inline constexpr std::size_t max_mesh_inputs = 32;
inline constexpr std::uint64_t mesh_bytes = 64 * 1024 * 1024;
inline constexpr MeshLimits mesh_limits{32 * 1024 * 1024, 65536, 49152, 512, 64, 0, 16};
struct Surface {
    EntityId entity;
    AffineTransform transform;
    unsigned primitive = no_primitive;
    AssetId mesh;
};
struct Sources {
    AssetId scene;
    nlohmann::json identities = nlohmann::json::array();
    std::vector<Surface> surfaces;
    std::set<AssetId> meshes;
};
Sources capture(const nlohmann::json& effective_scene);
nlohmann::json mesh_inputs(const Sources&, const std::function<std::string(AssetId)>& revision);
std::string source_digest(const Sources&, const nlohmann::json& mesh_inputs);
struct Geometry {
    std::vector<float> vertices;
    std::vector<int> indices;
    nlohmann::json sources = nlohmann::json::array();
    AssetId scene;
    std::string digest;
};
Geometry geometry(const nlohmann::json& effective_scene);
Geometry geometry(const Sources&); // Original primitive-only v1 canonical bytes.
Geometry geometry(const Sources&, const nlohmann::json& mesh_inputs,
                  const std::function<const MeshResourceData&(AssetId)>& mesh);
std::vector<std::byte> build_tile(const Geometry&, NavigationSettings);
} // namespace forge::navigation_detail
