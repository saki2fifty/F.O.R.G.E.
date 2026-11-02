#pragma once
#include "../navigation_collision_source.hpp"
#include "../navigation_region_value.hpp"
#include "../navigation_tile_geometry.hpp"
#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
namespace forge {
using NavigationTileContours = std::function<navigation_detail::RegionContours(
    const navigation_detail::NavigationColliderSource&, const AffineTransform&, bool)>;
// Detached view of one effective/preview row. Contours are never authored state.
struct NavigationRegionView {
    std::string entity;
    NavigationRegion2D region;
    AffineTransform transform;
    navigation_detail::RegionContours contours;
    Double3 center() const {
        if (region.source == 0 && region.points.empty())
            return {transform.m[3], transform.m[7], 0};
        auto low = contours.front().front(), high = low;
        for (const auto& contour : contours)
            for (const auto& point : contour)
                for (unsigned axis = 0; axis < 2; ++axis) {
                    low[axis] = std::min(low[axis], point[axis]);
                    high[axis] = std::max(high[axis], point[axis]);
                }
        return {(low[0] + high[0]) * .5, (low[1] + high[1]) * .5, 0};
    }
    Double3 axis(unsigned dimension) const {
        if (dimension > 1)
            throw std::out_of_range("Navigation region dimension");
        return {transform.m[dimension], transform.m[4 + dimension], 0};
    }
};
inline std::optional<NavigationRegionView>
navigation_region_view(const nlohmann::json& row, const NavigationTileContours& tiles = {}) {
    try {
        if (!row.is_object() || !row.at("id").is_string() || row.value("prefab", false) ||
            !row.value("spatial_resolved", false) || !row.at("components").is_object() ||
            !row.at("components").contains("forge.navigation_region_2d") ||
            !row.at("world_affine").is_array() || row.at("world_affine").size() != 12)
            return {};
        const auto& values = row.at("components").at("forge.navigation_region_2d");
        NavigationRegionView result;
        result.entity = row.at("id");
        result.region = navigation_detail::region_from_value(values);
        result.transform.m = row.at("world_affine").get<std::array<double, 12>>();
        (void)decompose(result.transform);
        if (result.region.source == 0)
            result.contours.push_back(
                navigation_detail::region_outline(result.region, result.transform));
        else {
            const auto collider =
                navigation_detail::collider_source(row.at("components"), result.transform);
            if (collider.kind == 3) {
                if (!tiles)
                    return {};
                result.contours = tiles(collider, result.transform, result.region.obstacle);
            } else
                result.contours.push_back(navigation_detail::collider_outline_2d(
                    collider.kind, collider.dimensions, result.transform, result.region.obstacle));
        }
        if (result.contours.empty() ||
            std::any_of(result.contours.begin(), result.contours.end(),
                        [](const auto& contour) { return contour.size() < 3; }))
            return {};
        return result;
    } catch (const std::exception&) {
        return {}; // No invented footprint for invalid or unresolved source geometry.
    }
}
inline std::optional<NavigationRegionView>
navigation_region_view(const nlohmann::json& source, const std::string& entity,
                       const NavigationTileContours& tiles = {}) {
    if (!source.is_object() || !source.contains("entities") || !source.at("entities").is_array())
        return {};
    for (const auto& row : source.at("entities"))
        if (row.is_object() && row.value("id", nlohmann::json()) == entity)
            return navigation_region_view(row, tiles);
    return {};
}
} // namespace forge
