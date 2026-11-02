#pragma once
#include <cstddef>
#include <cstdint>
#include <forge/asset_ref.hpp>
#include <forge/navigation_behavior_components.hpp>
#include <vector>
namespace forge {
enum class NavigationPlane : std::uint32_t { Spatial3D, PlanarXY };
struct NavMeshAsset {
    static constexpr const char* type = "navmesh";
};
struct NavigationSurface {
    bool enabled = true;
    bool operator==(const NavigationSurface&) const = default;
};
inline constexpr std::size_t navigation_region_vertex_limit = 64;
struct NavigationRegionVertex {
    float x = 0, y = 0;
    bool operator==(const NavigationRegionVertex&) const = default;
};
// Explicit static navigation geometry in world XY. Source selects authored
// rectangle/Points or this entity's configured Collider 2D; no body is created.
struct NavigationRegion2D {
    bool enabled = true, obstacle = false;
    float width = 10, height = 10;
    // Empty uses centered width/height. Otherwise,3..64 simple CCW local points.
    std::vector<NavigationRegionVertex> points{};
    std::uint32_t source = 0; // Rectangle/Points, or this entity's Collider 2D.
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
