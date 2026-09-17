#pragma once
#include "widgets.hpp"
#include <forge/authoring.hpp>
namespace forge {
inline const char* physics_label(const std::string& key) {
    if (key == "forge.physics_body")
        return "Physics Body";
    if (key == "forge.box_collider")
        return "Box Collider";
    if (key == "forge.sphere_collider")
        return "Sphere Collider";
    return "Capsule Collider";
}
inline void physics_inspector(Scene& scene, const std::string& selected, std::string& message) {
    if (selected.empty())
        return;
    try {
        Json item;
        const auto effective = scene.effective_document();
        for (const auto& e : effective.at("entities"))
            if (e.at("id") == selected)
                item = e;
        if (item.is_null())
            return;
        const auto components = item.at("components");
        if (ImGui::TreeNode("Physics")) {
            ui::help("Optional body and collider components. Simulation runs only in Play. "
                     "Collider dimensions are independent of the visible mesh.");
            if (ImGui::BeginCombo("Add component", "Choose physics component")) {
                for (const char* key : {"forge.physics_body", "forge.box_collider",
                                        "forge.sphere_collider", "forge.capsule_collider"}) {
                    if (ImGui::Selectable(physics_label(key), false,
                                          components.contains(key) ? ImGuiSelectableFlags_Disabled
                                                                   : 0))
                        authoring_command(scene, "component.add",
                                          {{"entity", selected}, {"component", key}});
                    ui::help("Add this component with defaults in one undoable edit. A body "
                             "requires one collider; dynamic bodies require Child space: World.");
                }
                ImGui::EndCombo();
            }
            ui::help("Add a body, then one collider. Adding physics does not change the object's "
                     "hierarchy or spatial binding.");
            const auto schema = scene.schema();
            for (const auto& type : schema.at("components")) {
                const std::string key = type.at("id");
                if ((key != "forge.physics_body" && !key.ends_with("_collider")) ||
                    !components.contains(key))
                    continue;
                ImGui::PushID(key.c_str());
                ui::heading(physics_label(key),
                            "Inherited values follow prefab source; editing a field creates "
                            "explicit override intent. Revert restores inheritance.");
                for (const auto& field : type.at("fields")) {
                    const std::string name = field.at("id");
                    auto value = components.at(key).at(name);
                    bool changed = false;
                    if (field.contains("enum")) {
                        int n = value.get<int>();
                        const auto labels = field.at("enum").get<std::vector<std::string>>();
                        if (ImGui::BeginCombo(name.c_str(), labels.at(n).c_str())) {
                            for (unsigned i = 0; i < labels.size(); ++i) {
                                if (ImGui::Selectable(labels[i].c_str(), n == int(i))) {
                                    value = i;
                                    changed = true;
                                }
                                ui::help(
                                    "Static stays fixed; Kinematic follows gameplay targets; "
                                    "Dynamic is driven by Jolt and requires Child space: World.");
                            }
                            ImGui::EndCombo();
                        }
                    } else {
                        double n = value.get<double>();
                        if (ImGui::InputDouble(name.c_str(), &n, 0, 0, "%.4f",
                                               ImGuiInputTextFlags_EnterReturnsTrue)) {
                            value = n;
                            changed = true;
                        }
                    }
                    const std::string help = field.at("description").get<std::string>() +
                                             ". Units: " + field.at("unit").get<std::string>() +
                                             ". Press Enter to commit a numeric edit.";
                    ui::help(help.c_str());
                    if (changed)
                        authoring_command(scene, "property.set",
                                          {{"entity", selected},
                                           {"component", key},
                                           {"field", name},
                                           {"value", value}});
                }
                if (ui::button("Remove / Revert component",
                               "Remove this owned component or its override intent. An inherited "
                               "prefab component becomes visible again. Undo restores this edit."))
                    authoring_command(scene, "component.revert",
                                      {{"entity", selected}, {"component", key}});
                ImGui::PopID();
            }
            if (components.contains("forge.physics_body")) {
                const auto motion =
                    components.at("forge.physics_body").at("motion").get<unsigned>();
                if (motion == 2 &&
                    item.value("spatial", Json::object()).value("mode", "follow_structure") !=
                        "world")
                    ImGui::TextWrapped("Dynamic body: set Child space to World before Play.");
                unsigned shapes = 0;
                for (const char* k :
                     {"forge.box_collider", "forge.sphere_collider", "forge.capsule_collider"})
                    shapes += components.contains(k);
                if (shapes != 1)
                    ImGui::TextWrapped("A Physics Body requires exactly one collider before Play.");
                ui::help("Play validates collision dimensions, scale and spatial binding. "
                         "Unsupported shear and nonuniform sphere/capsule scale are rejected.");
            }
            ImGui::TreePop();
        } else
            ui::help("Expand to add and edit physics bodies and primitive colliders.");
    } catch (const std::exception& e) {
        message = e.what();
    }
}
} // namespace forge
