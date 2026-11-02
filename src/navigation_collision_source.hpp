#pragma once
#include "navigation_collision_geometry.hpp"
#include <forge/identity.hpp>
#include <nlohmann/json.hpp>

namespace forge::navigation_detail {
struct NavigationColliderSource {
    unsigned kind = 0; // Box, Circle, Capsule, Tile Map.
    std::array<float, 2> dimensions{};
    AssetId map;
    std::uint64_t layer = 0;
    bool operator==(const NavigationColliderSource&) const = default;
};
inline NavigationColliderSource collider_source(const nlohmann::json& components,
                                                const AffineTransform& transform) {
    constexpr const char* keys[]{"forge.box_collider_2d", "forge.circle_collider_2d",
                                 "forge.capsule_collider_2d", "forge.tile_map_collider_2d"};
    NavigationColliderSource value;
    unsigned count = 0;
    for (unsigned i = 0; i < 4; ++i)
        if (components.contains(keys[i])) {
            ++count;
            value.kind = i;
        }
    if (count != 1)
        throw std::runtime_error("Collider navigation requires exactly one Collider 2D");
    const auto& data = components.at(keys[value.kind]);
    if (value.kind == 3) {
        const auto& layer = data.at("layer");
        if (!layer.is_number_integer() || layer <= 0)
            throw std::runtime_error(
                "Collider navigation requires a selected Tile Map collision layer");
        value.layer = layer.get<std::uint64_t>();
        if (!components.contains("forge.tile_map_renderer") ||
            components.at("forge.tile_map_renderer").at("map").is_null())
            throw std::runtime_error("Collider navigation requires a published Tile Map Renderer");
        value.map = components.at("forge.tile_map_renderer").at("map").get<AssetId>();
        (void)physics_detail::debug_pose_2d(transform);
        return value;
    }
    value.dimensions = value.kind == 0 ? std::array<float, 2>{data.at("x"), data.at("y")}
                       : value.kind == 1
                           ? std::array<float, 2>{data.at("radius"), 0}
                           : std::array<float, 2>{data.at("radius"), data.at("height")};
    // Runtime staleness samples only bounded scalar configuration. Hull construction
    // belongs to bake/view consumers, never the per-tick native source scan.
    const auto pose = physics_detail::debug_pose_2d(transform);
    (void)physics_detail::collider_dimensions_2d(value.kind, value.dimensions, pose.scale);
    return value;
}
inline nlohmann::json collider_source_value(const NavigationColliderSource& value) {
    if (value.kind == 3)
        return {{"kind", value.kind}, {"map", value.map}, {"layer", value.layer}};
    return {{"kind", value.kind}, {"dimensions", value.dimensions}};
}
} // namespace forge::navigation_detail
