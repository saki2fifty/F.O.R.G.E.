#pragma once
#include "audio_inspector.hpp"
#include <forge/ui_assets.hpp>
namespace forge {
inline void runtime_ui_inspector(Scene& scene, SceneDocument& project, const std::string& selected,
                                 std::string& message) {
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
        if (ImGui::TreeNode("Runtime UI")) {
            struct Scope {
                ~Scope() { ImGui::TreePop(); }
            } scope;
            ui::help("RmlUi game documents displayed in Play. Configure the asset, visibility and "
                     "drawing layer.");
            constexpr const char* key = "forge.ui_document";
            if (!item.at("components").contains(key)) {
                if (ui::button("Add UI Document",
                               "Add an optional UI Document in one undoable scene edit."))
                    authoring_command(scene, "component.add",
                                      {{"entity", selected}, {"component", key}});
            } else {
                auto schema = scene.schema();
                for (const auto& type : schema.at("components"))
                    if (type.at("id") == key)
                        for (const auto& field : type.at("fields")) {
                            const std::string name = field.at("id");
                            auto value = item["components"][key][name];
                            if (audio_field(project.project(), field, value))
                                authoring_command(scene, "property.set",
                                                  {{"entity", selected},
                                                   {"component", key},
                                                   {"field", name},
                                                   {"value", value}});
                        }
                if (ui::button(
                        "Remove / Revert UI Document",
                        "Remove owned UI Document configuration or its prefab overrides; scene "
                        "Undo restores this edit."))
                    authoring_command(scene, "component.revert",
                                      {{"entity", selected}, {"component", key}});
                ImGui::TextWrapped(
                    "Create or register RML in Content > Runtime UI. Play displays the document; "
                    "Capture gameplay input lets you interact with it.");
                ui::help("UI assets are separate from scene Undo. Document fields support normal "
                         "prefab overrides and Revert.");
            }
        } else
            ui::help("Add a runtime UI document to this object.");
    } catch (const std::exception& e) {
        message = e.what();
    }
}
class RuntimeUiTools {
    char source_[1024]{};

  public:
    void content(SceneDocument& project, bool locked, std::string& message) {
        if (!ImGui::CollapsingHeader("Runtime UI")) {
            ui::help("Create a game HUD example or register an existing project RML document.");
            return;
        }
        ui::help("RmlUi game documents and styles. Assign a registered document in the Inspector; "
                 "test it in Play.");
        ImGui::BeginDisabled(locked);
        try {
            if (ui::button("Create HUD example",
                           "Create and register a HUD with text, buttons, an image, model values "
                           "and a text field. This creates assets, outside scene Undo.")) {
                project.check_ownership();
                auto record = create_ui_example(project.project());
                message = "HUD registered: " + path_utf8(record.source) +
                          ". Add UI Document to an object and select it.";
            }
            ImGui::InputText("RML in project", source_, sizeof(source_));
            ui::help("Path relative to the project, for example Assets/UI/hud.rml. Styles, fonts "
                     "and TGA images must stay within the project.");
            if (ui::button("Register RML",
                           "Validate and register this document with a persistent AssetId. Assign "
                           "it using UI Document in the Inspector.")) {
                project.check_ownership();
                auto record =
                    register_ui_document(project.project(), std::filesystem::u8path(source_));
                message = "UI document registered: " + path_utf8(record.source);
            }
        } catch (const std::exception& e) {
            message = e.what();
        }
        ImGui::EndDisabled();
    }
};
} // namespace forge
