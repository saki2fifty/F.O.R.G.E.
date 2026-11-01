#pragma once
#include <cstdint>
#include <forge/asset_ref.hpp>
namespace forge {
enum class NavigationPlane : std::uint32_t { Spatial3D, PlanarXY };
struct NavMeshAsset {
    static constexpr const char* type = "navmesh";
};
struct NavigationSurface {
    bool enabled = true;
    bool operator==(const NavigationSurface&) const = default;
};
// Independent, centered navigation geometry in world XY; not a physics collider.
struct NavigationRegion2D {
    bool enabled = true, obstacle = false;
    float width = 10, height = 10;
    bool operator==(const NavigationRegion2D&) const = default;
};
// Runtime-carved upright cylinder; an XY profile uses its circular footprint.
struct NavigationObstacle {
    bool enabled = true;
    float radius = .5f, height = 2;
    bool operator==(const NavigationObstacle&) const = default;
};
struct NavigationAgent {
    AssetRef<NavMeshAsset> navmesh;
    bool enabled = true, has_destination = false;
    float speed = 2, stopping_distance = .1f;
    double destination_x = 0, destination_y = 0, destination_z = 0;
    bool operator==(const NavigationAgent&) const = default;
};
struct NavigationAvoidance {
    bool enabled = true;
    float horizon = 2;
    bool operator==(const NavigationAvoidance&) const = default;
};
} // namespace forge
