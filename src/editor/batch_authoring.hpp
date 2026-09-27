#pragma once
#include <algorithm>
#include <forge/authoring.hpp>
#include <map>
#include <set>
#include <vector>
namespace forge {
// Normalize by the hierarchy used by the operation, not by row order.
inline std::vector<std::string>
selection_roots(const Json& doc, const std::vector<std::string>& selection, bool spatial) {
    std::map<std::string, std::string> parents;
    for (const auto& row : doc.at("entities")) {
        auto parent = row.value("parent", std::string{});
        if (spatial && row.contains("spatial")) {
            const auto& binding = row.at("spatial");
            const auto mode = binding.value("mode", std::string("follow_structure"));
            if (mode == "world")
                parent.clear();
            if (mode == "explicit") {
                const auto target = binding.at("target").get<EntityRef>();
                parent = target.scene == doc.at("asset_id").get<AssetId>() ? target.entity.str()
                                                                           : std::string{};
            }
        }
        parents.emplace(row.at("id").get<std::string>(), parent);
    }
    const std::set<std::string> selected(selection.begin(), selection.end());
    std::vector<std::string> roots;
    for (const auto& id : selection) {
        if (!parents.contains(id))
            throw std::runtime_error("Selected entity no longer exists");
        auto parent = parents.at(id);
        std::set<std::string> visited{id};
        bool covered = false;
        while (!parent.empty()) {
            if (!visited.insert(parent).second)
                throw std::runtime_error("Selection hierarchy cycle");
            if (selected.contains(parent)) {
                covered = true;
                break;
            }
            const auto found = parents.find(parent);
            if (found == parents.end())
                break;
            parent = found->second;
        }
        if (!covered && std::find(roots.begin(), roots.end(), id) == roots.end())
            roots.push_back(id);
    }
    return roots;
}
inline Json selection_commands(const Scene& scene, const std::vector<std::string>& ids,
                               const std::string& operation, Json arguments = Json::object()) {
    const auto targets = operation == "entity.duplicate" || operation == "entity.delete"
                             ? selection_roots(scene.document(), ids, false)
                             : ids;
    Json commands = Json::array();
    for (const auto& id : targets) {
        arguments["entity"] = id;
        commands.push_back({{"operation", operation}, {"arguments", arguments}});
    }
    return commands;
}
} // namespace forge
