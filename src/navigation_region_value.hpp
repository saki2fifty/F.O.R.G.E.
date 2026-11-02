#pragma once
#include "navigation_region.hpp"
#include <nlohmann/json.hpp>

namespace forge::navigation_detail {
inline nlohmann::json region_value(const NavigationRegion2D& region) {
    if (region.points.size() > navigation_region_vertex_limit)
        throw std::runtime_error("Navigation polygon exceeds64 points");
    auto points = nlohmann::json::array();
    for (const auto& point : region.points)
        points.push_back({{"x", point.x}, {"y", point.y}});
    return {{"enabled", region.enabled},
            {"obstacle", region.obstacle},
            {"width", region.width},
            {"height", region.height},
            {"points", points}};
}
inline NavigationRegion2D region_from_value(const nlohmann::json& value) {
    NavigationRegion2D region{value.at("enabled"), value.at("obstacle"), value.at("width"),
                              value.at("height")};
    if (value.contains("points")) {
        const auto& points = value.at("points");
        if (!points.is_array() || points.size() > navigation_region_vertex_limit)
            throw std::runtime_error("Navigation polygon requires an array of at most64 points");
        region.points.reserve(points.size());
        for (const auto& point : points)
            region.points.push_back({point.at("x").get<float>(), point.at("y").get<float>()});
    }
    if (region.points.empty())
        validate_region_dimensions(region);
    else
        (void)region_local_points(region);
    return region;
}
} // namespace forge::navigation_detail
