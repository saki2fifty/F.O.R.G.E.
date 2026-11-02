#pragma once
#include <forge/navigation_build.hpp>
#include <nlohmann/json.hpp>

namespace forge::navigation_detail {
// A copied selection decision shared by evaluated Scene and native capture.
// Shape/ancestry admission remains with their existing owners after selection.
inline bool collect_static_collider(const nlohmann::json& components,
                                    const NavigationSettings& settings) {
    if (!settings.collect_static_colliders)
        return false;
    if (settings.plane != NavigationPlane::Spatial3D && settings.plane != NavigationPlane::PlanarXY)
        throw std::runtime_error("Unsupported navigation collision collection plane");
    const bool xy = settings.plane == NavigationPlane::PlanarXY;
    const char* marker = xy ? "forge.navigation_region_2d" : "forge.navigation_surface";
    // A disabled marker is an explicit opt-out; do not silently reinclude it.
    if (components.contains(marker))
        return false;
    const char* body = xy ? "forge.physics_body_2d" : "forge.physics_body";
    if (!components.contains(body))
        return false;
    const auto& value = components.at(body);
    if (!value.value("enabled", true) || value.value("sensor", false))
        return false;
    const auto motion = value.value("motion", nlohmann::json(0));
    if (!motion.is_number_integer() || motion < 0 || motion > 2)
        throw std::runtime_error(
            "Navigation collision body motion must be Static, Kinematic or Dynamic");
    if (motion != 0)
        return false;
    const auto& layer = value.contains("layer") ? value.at("layer") : nlohmann::json(0);
    if (!layer.is_number_integer() || layer < 0 || layer > 31)
        throw std::runtime_error("Navigation collision source layer must be an integer 0..31");
    return (settings.collision_layers & (std::uint32_t(1) << layer.get<unsigned>())) != 0;
}
} // namespace forge::navigation_detail
