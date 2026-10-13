#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
namespace forge::asset_detail {
// Optional source-owned editor metadata. State IDs remain the only node identity.
inline bool has_state_graph_layout(const nlohmann::json& source) {
    const auto at = source.find("editor_layout");
    return at != source.end() && at->is_object() && at->contains("format") &&
           at->at("format") == "forge.animation-state-layout";
}
inline void validate_state_graph_layout(const nlohmann::json& source) {
    if (!has_state_graph_layout(source))
        return; // Preserve an unrecognized extension without interpreting it.
    const auto& layout = source.at("editor_layout");
    if (layout.at("version") != 1 || !layout.at("positions").is_array() ||
        layout.at("positions").size() > 32)
        throw std::runtime_error("Unsupported animation state graph layout or too many positions");
    std::set<std::uint32_t> states, positions;
    for (const auto& row : source.at("states"))
        states.insert(row.at("id").get<std::uint32_t>());
    for (const auto& row : layout.at("positions")) {
        const auto& value = row.at("state");
        if ((!value.is_number_unsigned() &&
             !(value.is_number_integer() && value.get<std::int64_t>() > 0)) ||
            value.get<std::uint64_t>() > UINT32_MAX)
            throw std::runtime_error("Graph layout needs a stable state ID");
        const auto id = value.get<std::uint32_t>();
        if (!states.contains(id) || !positions.insert(id).second)
            throw std::runtime_error("Graph layout state is missing or duplicated");
        for (const auto* field : {"x", "y"}) {
            const auto& coordinate = row.at(field);
            if (!coordinate.is_number())
                throw std::runtime_error("Graph layout coordinates must be numeric");
            const double number = coordinate.get<double>();
            if (!std::isfinite(number) || std::abs(number) > 1000000)
                throw std::runtime_error("Graph layout coordinates must be finite within ±1000000");
        }
    }
}
inline std::array<double, 2> state_graph_position(const nlohmann::json& source, std::uint32_t state,
                                                  std::size_t ordinal) {
    if (has_state_graph_layout(source))
        for (const auto& row : source.at("editor_layout").at("positions"))
            if (row.at("state") == state)
                return {row.at("x").get<double>(), row.at("y").get<double>()};
    return {double(ordinal % 4) * 300, double(ordinal / 4) * 150};
}
inline void set_state_graph_position(nlohmann::json& source, std::uint32_t state,
                                     std::array<double, 2> position) {
    if (source.contains("editor_layout") && !has_state_graph_layout(source))
        throw std::runtime_error(
            "Rename the opaque editor_layout extension before editing graph positions");
    if (!source.contains("editor_layout"))
        source["editor_layout"] = {{"format", "forge.animation-state-layout"},
                                   {"version", 1},
                                   {"positions", nlohmann::json::array()}};
    auto& rows = source["editor_layout"]["positions"];
    for (auto& row : rows)
        if (row.at("state") == state) {
            row["x"] = position[0];
            row["y"] = position[1];
            return;
        }
    rows.push_back({{"state", state}, {"x", position[0]}, {"y", position[1]}});
}
inline void remove_state_graph_position(nlohmann::json& source, std::uint32_t state) {
    if (has_state_graph_layout(source))
        std::erase_if(source["editor_layout"]["positions"].get_ref<nlohmann::json::array_t&>(),
                      [&](const auto& row) { return row.at("state") == state; });
}
} // namespace forge::asset_detail
