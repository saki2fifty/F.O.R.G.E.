#pragma once
#include "builtins.hpp"
#include <algorithm>
#include <set>

namespace forge::detail {
inline void clear_prefab_component_suppression(Json& row, const std::string& component) {
    if (!row.contains("suppressed_components"))
        return;
    auto& values = row["suppressed_components"];
    values.erase(std::remove(values.begin(), values.end(), Json(component)), values.end());
    if (values.empty())
        row.erase("suppressed_components");
}
inline void require_optional_prefab_component(const std::string& key, const Json& schema,
                                              bool require_admitted) {
    // Reuse the existing removal policy, including protected transform channels.
    const auto& components = schema.at("components");
    const auto type = std::find_if(components.begin(), components.end(),
                                   [&](const Json& item) { return item.at("id") == key; });
    if (key == "forge.world_transform" || key == "forge.position" || key == "forge.rotation" ||
        key == "forge.scale" || (type != components.end() && !type->value("optional", false)) ||
        (require_admitted && type == components.end()))
        throw std::runtime_error("Only admitted optional components can be suppressed");
}
inline void validate_prefab_component_suppression(const Json& row, const Json& schema) {
    if (!row.contains("suppressed_components"))
        return;
    const auto& values = row.at("suppressed_components");
    if (!values.is_array() || values.size() > 256)
        throw std::runtime_error("Prefab suppression requires at most 256 component keys");
    std::set<std::string> keys;
    for (const auto& value : values) {
        if (!value.is_string())
            throw std::runtime_error("Prefab suppression keys must be canonical component names");
        const auto key = value.get<std::string>();
        if (key.empty() || key.size() > 256 || !keys.insert(key).second ||
            row.at("components").contains(key) ||
            (row.contains("property_overrides") && row.at("property_overrides").contains(key)))
            throw std::runtime_error("Invalid or contradictory prefab component suppression");
        // Unavailable types retain explicit dormant intent. A later admission is
        // checked again before it can change a native template.
        require_optional_prefab_component(key, schema, false);
    }
}
} // namespace forge::detail
