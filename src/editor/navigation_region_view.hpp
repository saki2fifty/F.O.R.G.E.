#pragma once
#include "../navigation_region.hpp"
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
namespace forge {
// Detached view of one effective/preview row. Corners are never authored state.
struct NavigationRegionView {
    std::string entity;
    NavigationRegion2D region;
    AffineTransform transform;
    std::array<Double3, 4> quad;
    Double3 center() const { return {transform.m[3], transform.m[7], 0}; }
    Double3 axis(unsigned dimension) const {
        if (dimension > 1)
            throw std::out_of_range("Navigation region dimension");
        return {transform.m[dimension], transform.m[4 + dimension], 0};
    }
};
inline std::optional<NavigationRegionView> navigation_region_view(const nlohmann::json& row) {
    try {
        if (!row.is_object() || !row.at("id").is_string() || row.value("prefab", false) ||
            !row.value("spatial_resolved", false) || !row.at("components").is_object() ||
            !row.at("components").contains("forge.navigation_region_2d") ||
            !row.at("world_affine").is_array() || row.at("world_affine").size() != 12)
            return {};
        const auto& values = row.at("components").at("forge.navigation_region_2d");
        NavigationRegionView result;
        result.entity = row.at("id");
        result.region = {values.at("enabled").get<bool>(), values.at("obstacle").get<bool>(),
                         values.at("width").get<float>(), values.at("height").get<float>()};
        result.transform.m = row.at("world_affine").get<std::array<double, 12>>();
        (void)decompose(result.transform);
        result.quad = navigation_detail::region_quad(result.region, result.transform);
        return result;
    } catch (const std::exception&) {
        return {}; // No invented footprint for invalid or unresolved source geometry.
    }
}
inline std::optional<NavigationRegionView> navigation_region_view(const nlohmann::json& source,
                                                                  const std::string& entity) {
    if (!source.is_object() || !source.contains("entities") || !source.at("entities").is_array())
        return {};
    for (const auto& row : source.at("entities"))
        if (row.is_object() && row.value("id", nlohmann::json()) == entity)
            return navigation_region_view(row);
    return {};
}
} // namespace forge
