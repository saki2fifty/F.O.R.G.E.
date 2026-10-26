#pragma once
#include "spatial_component_value.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <forge/render_scene.hpp>
#include <optional>
#include <string>
namespace forge {
// Copied editor view. The navigation owner supplies the saved asset/plane/scope;
// no source pointer, runtime route, new identity or asset selection is retained.
struct NavigationDestinationView {
    std::filesystem::path project;
    AssetId scene;
    std::string entity;
    NavigationPlane plane = NavigationPlane::Spatial3D;
    NavigationAgent agent;
    Double3 origin{}, point{};
    bool selectable = true;
    bool operator==(const NavigationDestinationView&) const = default;
};
inline Double3 navigation_guide_point(Double3 point, NavigationPlane plane) {
    point[plane == NavigationPlane::PlanarXY ? 2 : 1] += .02;
    return point;
}
inline std::optional<NavigationDestinationView>
navigation_destination_view(const Json& source, const std::string& selected, AssetId asset,
                            NavigationPlane plane, const std::filesystem::path& project) {
    try {
        if (selected.empty() || !asset ||
            (plane != NavigationPlane::Spatial3D && plane != NavigationPlane::PlanarXY))
            return {};
        for (const auto& row : source.at("entities")) {
            if (row.at("id") != selected || row.value("prefab", false) ||
                !row.value("spatial_resolved", false) ||
                !row.at("components").contains("forge.navigation_agent"))
                continue;
            const auto agent = ui::spatial_component_value<NavigationAgent>(
                row.at("components"), "forge.navigation_agent");
            if (!agent.has_destination || agent.navmesh.id != asset)
                return {};
            const auto policies = extract_node_policies(source);
            const auto policy = policies.entities.find(EntityId::parse(selected));
            if (policy == policies.entities.end() || !policy->second.valid ||
                !policy->second.visible)
                return {};
            const auto world = row.at("world_affine").get<std::array<double, 12>>();
            if (!std::all_of(world.begin(), world.end(), [](double x) { return std::isfinite(x); }))
                return {};
            NavigationDestinationView view;
            view.project = project;
            view.scene = source.at("asset_id").get<AssetId>();
            view.entity = selected;
            view.plane = plane;
            view.agent = agent;
            view.origin = {world[3], world[7], world[11]};
            view.point = {agent.destination_x, agent.destination_y, agent.destination_z};
            if (plane == NavigationPlane::PlanarXY)
                view.point[2] = view.origin[2];
            view.selectable = policy->second.selectable;
            return view;
        }
    } catch (const std::exception&) {
        // Invalid/unresolved source has no invented destination guide.
    }
    return {};
}
} // namespace forge
