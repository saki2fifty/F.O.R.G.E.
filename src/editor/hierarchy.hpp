#pragma once
#include "widgets.hpp"
#include <forge/scene.hpp>
#include <functional>
#include <map>
namespace forge::ui {
inline void hierarchy(const Json& doc, std::string& selected) {
    std::map<std::string, std::vector<const Json*>> children;
    for (const auto& e : doc.at("entities"))
        children[e.value("parent", std::string{})].push_back(&e);
    std::function<void(const std::string&)> draw = [&](const std::string& parent) {
        for (const auto* e : children[parent]) {
            const auto id = e->at("id").get<std::string>();
            auto flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (id == selected)
                flags |= ImGuiTreeNodeFlags_Selected;
            if (children[id].empty())
                flags |= ImGuiTreeNodeFlags_Leaf;
            const bool open = ImGui::TreeNodeEx(
                id.c_str(), flags, "%s", e->at("name").get_ref<const std::string&>().c_str());
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
