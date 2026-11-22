#pragma once
#include <cstdint>
#include <forge/scene.hpp>
#include <forge/transform.hpp>
#include <vector>
namespace forge::detail {
class SceneDraft;
inline constexpr unsigned mesh_scatter_limit = 512;
struct MeshScatterSettings {
    Double3 center{};
    double radius = 5, spacing = .5, scale_min = .8, scale_max = 1.2;
    double yaw_min = 0, yaw_max = 360;
    std::uint32_t count = 32, seed = 1;
};
struct MeshScatterPlacement {
    Double2 offset;
    double scale, yaw;
    bool operator==(const MeshScatterPlacement&) const = default;
};
struct MeshScatterCandidate {
    LocalTransform transform;
    bool selected = true;
};
// Bounded candidate generation only; no identities, Scene state or hidden runtime data.
std::vector<MeshScatterPlacement> mesh_scatter_layout(const MeshScatterSettings&);
MeshScatterSettings mesh_scatter_settings(const Json& arguments);
// The exact group-local transform and mask later used by ordinary Scene placement.
// Source preparation consumers share this owner for height, yaw, slope and scale.
std::vector<MeshScatterCandidate> mesh_scatter_candidates(const LocalTransform& prototype,
                                                          const Json& arguments);
Json mesh_scatter_arguments(const std::string& source, const MeshScatterSettings&,
                            const std::string& name);
Json scatter_mesh(SceneDraft&, const std::string& source, const Json& arguments);
} // namespace forge::detail
