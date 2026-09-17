#pragma once
#include "search.hpp"
#include "widgets.hpp"
#include <forge/scene.hpp>
#include <functional>
#include <map>
#include <set>
namespace forge::ui {
inline std::set<std::string> hierarchy_matches(const Json& doc, const std::string& filter) {
    const auto needle = search_key(filter);
    std::map<std::string, std::string> parents;
    std::set<std::string> visible;
    for (const auto& e : doc.at("entities"))
        parents[e.at("id").get<std::string>()] = e.value("parent", std::string{});
    for (const auto& e : doc.at("entities")) {
        auto id = e.at("id").get<std::string>();
        if (needle.empty() ||
            search_key(e.at("name").get<std::string>()).find(needle) != std::string::npos ||
            search_key(id).find(needle) != std::string::npos)
            while (!id.empty() && visible.insert(id).second)
                id = parents[id];
    }
    return visible;
}
inline void hierarchy(const Json& doc, std::string& selected, const std::string& filter = "",
                      int expand = 0) {
    const auto visible = hierarchy_matches(doc, filter);
    std::map<std::string, std::vector<const Json*>> children;
    for (const auto& e : doc.at("entities"))
        if (visible.contains(e.at("id").get<std::string>()))
            children[e.value("parent", std::string{})].push_back(&e);
    for (auto& [parent, entries] : children)
        std::stable_sort(entries.begin(), entries.end(), [](const Json* a, const Json* b) {
            return search_key(a->at("name").get<std::string>()) <
                   search_key(b->at("name").get<std::string>());
        });
    std::function<void(const std::string&)> draw = [&](const std::string& parent) {
        for (const auto* e : children[parent]) {
            const auto id = e->at("id").get<std::string>();
            auto flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (id == selected)
                flags |= ImGuiTreeNodeFlags_Selected;
            if (children[id].empty())
                flags |= ImGuiTreeNodeFlags_Leaf;
            if (!filter.empty() || expand != 0)
                ImGui::SetNextItemOpen(!filter.empty() || expand > 0, ImGuiCond_Always);
            const bool open = ImGui::TreeNodeEx(id.c_str(), flags, "%s%s",
                                                e->at("name").get_ref<const std::string&>().c_str(),
                                                e->value("missing_member", false) ? " [missing]"
                                                : e->contains("prefab_instance")  ? " [prefab]"
                                                : e->contains("prefab_member")    ? " [member]"
                                                                                  : "");
            if (ImGui::IsItemClicked())
                selected = id;
            help("Select this authored entity. Expand the arrow to see its children.");
            if (open) {
                draw(id);
                ImGui::TreePop();
            }
        }
    };
    draw("");
}
} // namespace forge::ui
