#pragma once
#include <algorithm>
#include <forge/material_graph.hpp>
namespace forge::lightmap_test {
inline nlohmann::json graph(unsigned uv_set = 3) {
    auto result = create_material_graph();
    const auto surface = *std::find_if(result["nodes"].begin(), result["nodes"].end(),
                                       [](const auto& n) { return n.at("type") == "surface"; });
    for (auto& node : result["nodes"])
        node["position"] =
            node.at("id") == surface.at("id") ? nlohmann::json{840, 0} : nlohmann::json{560, -160};
    auto texture = create_material_graph_node("texture2d", {0, 120});
    texture["data"]["key"] = "lightmap";
    texture["data"]["label"] = "Imported irradiance";
    texture["data"]["semantic"] = "hdr";
    texture["data"]["uv_set"] = uv_set;
    auto convert = create_material_graph_node("convert", {280, 120});
    convert["data"]["from"] = "color4";
    convert["data"]["type"] = "color3";
    auto gain = create_material_graph_node("parameter", {280, 320});
    gain["data"]["key"] = "lightmap_gain";
    gain["data"]["label"] = "Lightmap gain";
    gain["data"]["type"] = "color3";
    gain["data"]["value"] = {1, 1, 1};
    auto multiply = create_material_graph_node("multiply", {560, 120});
    multiply["data"]["type"] = "color3";
    const auto link = [&](const auto& from, const auto& to, const char* input) {
        result["edges"].push_back({{"id", GraphEdgeId::generate()},
                                   {"from", {{"node", from.at("id")}, {"port", "out"}}},
                                   {"to", {{"node", to.at("id")}, {"port", input}}}});
    };
    link(texture, convert, "value");
    link(convert, multiply, "a");
    link(gain, multiply, "b");
    link(multiply, surface, "lightmap_irradiance");
    for (const auto& node : {texture, convert, gain, multiply})
        result["nodes"].push_back(node);
    return result;
}
} // namespace forge::lightmap_test
