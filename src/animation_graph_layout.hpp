#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <forge/identity.hpp>
#include <set>

namespace forge::asset_detail {
inline bool has_animation_graph_layout(const nlohmann::json& source) {
    const auto at = source.find("editor_layout");
    return at != source.end() && at->is_object() && at->contains("format") &&
           at->at("format") == "forge.animation-graph-layout";
}
inline void validate_animation_graph_layout(const nlohmann::json& source) {
    if (!has_animation_graph_layout(source))
        return;
    const auto& layout = source.at("editor_layout");
    const auto& positions = layout.at("positions");
    if (!layout.at("version").is_number_integer() || layout.at("version") != 1 ||
        !positions.is_array() || positions.size() > source.at("nodes").size())
        throw std::runtime_error("Animation Graph layout version or position count is invalid");
    std::set<AssetId> nodes, placed;
    for (const auto& node : source.at("nodes"))
        nodes.insert(node.at("id").get<AssetId>());
    for (const auto& row : positions) {
        const auto id = row.at("node").get<AssetId>();
        if (!nodes.contains(id) || !placed.insert(id).second)
            throw std::runtime_error("Animation Graph layout node is missing or duplicated");
        for (const auto* field : {"x", "y"}) {
            const auto& value = row.at(field);
            if (!value.is_number() || !std::isfinite(value.get<double>()) ||
                std::abs(value.get<double>()) > 1000000)
                throw std::runtime_error(
                    "Animation Graph positions must be finite within ±1000000");
        }
    }
}
inline std::array<double, 2> animation_graph_position(const nlohmann::json& source, AssetId node,
                                                      std::size_t ordinal) {
    if (has_animation_graph_layout(source))
        for (const auto& row : source.at("editor_layout").at("positions"))
            if (row.at("node").get<AssetId>() == node)
                return {row.at("x").get<double>(), row.at("y").get<double>()};
    return {double(ordinal % 4) * 300, double(ordinal / 4) * 150};
}
inline void set_animation_graph_position(nlohmann::json& source, AssetId node,
                                         std::array<double, 2> position) {
    if (source.contains("editor_layout") && !has_animation_graph_layout(source))
        throw std::runtime_error(
            "Rename the opaque editor_layout extension before editing graph positions");
    if (!source.contains("editor_layout"))
        source["editor_layout"] = {{"format", "forge.animation-graph-layout"},
                                   {"version", 1},
                                   {"positions", nlohmann::json::array()}};
    auto& rows = source["editor_layout"]["positions"];
    for (auto& row : rows)
        if (row.at("node").get<AssetId>() == node) {
            row["x"] = position[0];
            row["y"] = position[1];
            return;
        }
    rows.push_back({{"node", node}, {"x", position[0]}, {"y", position[1]}});
}
inline void remove_animation_graph_position(nlohmann::json& source, AssetId node) {
    if (has_animation_graph_layout(source))
        std::erase_if(
            source["editor_layout"]["positions"].get_ref<nlohmann::json::array_t&>(),
            [&](const auto& row) { return row.at("node").template get<AssetId>() == node; });
}
} // namespace forge::asset_detail
