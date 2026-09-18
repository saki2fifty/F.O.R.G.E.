#pragma once
#include <forge/assets.hpp>
namespace forge {
struct NavMeshAsset {
    static constexpr const char* type = "navmesh";
};
struct NavigationSurface {
    bool enabled = true;
    bool operator==(const NavigationSurface&) const = default;
};
struct NavigationAgent {
    AssetRef<NavMeshAsset> navmesh;
    bool enabled = true, has_destination = false;
    float speed = 2, stopping_distance = .1f;
    double destination_x = 0, destination_y = 0, destination_z = 0;
    bool operator==(const NavigationAgent&) const = default;
};
} // namespace forge
