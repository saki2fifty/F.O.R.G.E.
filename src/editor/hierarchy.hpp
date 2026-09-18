#pragma once
#include "editor_state.hpp"
#include "search.hpp"
#include "widgets.hpp"
#include <forge/authoring.hpp>
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
                      int expand = 0, Scene* scene = nullptr, bool locked = false) {
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
            if (ImGui::IsItemClicked() || ImGui::IsItemClicked(ImGuiMouseButton_Right))
                selected = id;
            help("Select this authored entity. Expand the arrow to see its children.");
            if (scene && !locked && !e->contains("prefab_member") && ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload("FORGE_ENTITY", id.c_str(), id.size() + 1);
                ImGui::Text("Parent: %s", e->at("name").get_ref<const std::string&>().c_str());
                ImGui::EndDragDropSource();
            }
            if (scene && !locked && ImGui::BeginDragDropTarget()) {
                if (const auto* payload = ImGui::AcceptDragDropPayload(
                        "FORGE_ENTITY", ImGuiDragDropFlags_AcceptBeforeDelivery)) {
                    if (payload->DataSize == 37) {
                        const std::string child(static_cast<const char*>(payload->Data), 36);
                        const Json arguments = {{"entity", child}, {"parent", id}};
                        try {
                            (void)preview_authoring(*scene,
                                                    Json::array({{{"operation", "entity.reparent"},
                                                                  {"arguments", arguments}}}));
                            ImGui::SetTooltip(
                                "Reparent preserving world placement; space becomes Follow parent");
                            if (payload->IsDelivery()) {
                                authoring_command(*scene, "entity.reparent", arguments);
                                selected = child;
                            }
                        } catch (const std::exception& error) {
                            ImGui::SetTooltip("Cannot parent: %s", error.what());
                            if (payload->IsDelivery() && editor_context)
                                editor_context->problems.report({"hierarchy/" + child,
                                                                 "Error",
                                                                 error.what(),
                                                                 child,
                                                                 {},
                                                                 "parent",
                                                                 scene->asset_id()});
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }
            if (open) {
                draw(id);
                ImGui::TreePop();
            }
        }
    };
    if (visible.empty())
        ImGui::TextWrapped(filter.empty()
                               ? "No entities. Use Entity > Create or the Scene Create menu."
                               : "No matching entities. Clear the search to show the scene.");
    draw("");
}
} // namespace forge::ui
