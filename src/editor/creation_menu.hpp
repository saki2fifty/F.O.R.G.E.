#pragma once
#include "actions.hpp"
#include <forge/entity_recipes.hpp>
#include <set>
namespace forge::ui {
inline bool creation_menu(const EditorActions& actions, bool& at_target) {
    std::set<std::string> categories;
    for (const auto& r : entity_recipes()) {
        if (r.category.empty())
            actions.item("Create / " + r.label, r.label.c_str());
        else
            categories.insert(r.category);
    }
    for (const auto& c : categories)
        if (ImGui::BeginMenu(c.c_str())) {
            for (const auto& r : entity_recipes())
                if (r.category == c)
                    actions.item("Create / " + r.label, r.label.c_str());
            ImGui::EndMenu();
        }
    ImGui::Separator();
    bool changed = false;
    if (ImGui::BeginMenu(at_target ? "Placement: At View Target" : "Placement: World Origin")) {
        if (ImGui::MenuItem("At View Target", nullptr, at_target)) {
            at_target = true;
            changed = true;
        }
        help("Create at the Scene camera pivot. Creation is one scene Undo step.");
        if (ImGui::MenuItem("World Origin", nullptr, !at_target)) {
            at_target = false;
            changed = true;
        }
        help("Create at world position 0, 0, 0. This is not ground placement.");
        ImGui::EndMenu();
    }
    return changed;
}
} // namespace forge::ui
