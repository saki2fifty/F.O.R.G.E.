#pragma once
#include <forge/scene_identity.hpp>
#include <map>
#include <set>
namespace forge::detail {
// Detached single-scene operation shared by UI and automation. No catalog or
// source mutation; resource admission separately verifies Model membership.
inline std::size_t edit_model_material_variant(Json& authored, const Json& effective,
                                               const std::string& root, const Json& variant) {
    std::map<std::string, const Json*> rows;
    std::map<std::string, std::vector<std::string>> children;
    for (const auto& row : effective.at("entities")) {
        const auto id = row.at("id").get<std::string>();
        rows.emplace(id, &row);
        if (row.contains("parent") && !row.at("parent").is_null())
            children[row.at("parent").get<std::string>()].push_back(id);
    }
    const auto found = rows.find(root);
    if (found == rows.end())
        throw std::runtime_error("Model instance root is missing");
    const auto& components = found->second->at("components");
    if (!components.contains("forge.model_source") ||
        !components.at("forge.model_source").at("node").is_null() ||
        components.at("forge.model_source").at("model").is_null())
        throw std::runtime_error("Select the placed Model root to change its material variant");
    const auto model = components.at("forge.model_source").at("model");
    std::set<std::string> visited, targets;
    std::vector<std::string> pending{root};
    while (!pending.empty()) {
        const auto id = std::move(pending.back());
        pending.pop_back();
        if (!visited.insert(id).second)
            throw std::runtime_error("Model structural ancestry is cyclic");
        const auto& row = *rows.at(id);
        if (row.value("prefab", false))
            continue;
        const auto& values = row.at("components");
        if (values.contains("forge.model_source")) {
            const auto& source = values.at("forge.model_source");
            if (id != root && source.at("node").is_null())
                continue; // Independent nested placement.
            if (source.at("model") != model)
                throw std::runtime_error("Model node belongs to another Model root");
            if (values.contains("forge.mesh_renderer"))
                targets.insert(id);
        }
        if (const auto it = children.find(id); it != children.end())
            pending.insert(pending.end(), it->second.begin(), it->second.end());
    }
    for (auto& row : authored["entities"]) {
        if (!targets.contains(row.at("id").get<std::string>()))
            continue;
        constexpr const char* component = "forge.mesh_renderer";
        const bool property_intent =
            (row.contains("prefab_instance") || row.contains("prefab_member")) &&
            !row.at("components").contains(component);
        if (property_intent)
            row["property_overrides"][component]["material_variant"] = variant;
        else
            row["components"][component]["material_variant"] = variant;
    }
    return targets.size();
}
} // namespace forge::detail
