#pragma once
#include <forge/navigation_build.hpp>
namespace forge::navigation_detail {
struct Geometry {
    std::vector<float> vertices;
    std::vector<int> indices;
    nlohmann::json sources = nlohmann::json::array();
    AssetId scene;
    std::string digest;
};
Geometry geometry(const nlohmann::json& effective_scene);
std::vector<std::byte> build_tile(const Geometry&, NavigationSettings);
} // namespace forge::navigation_detail
