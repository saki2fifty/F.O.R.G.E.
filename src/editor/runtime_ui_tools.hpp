#pragma once
#include "document.hpp"
#include "property_drawer.hpp"
#include <forge/authoring.hpp>
#include <forge/ui_assets.hpp>
namespace forge {
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
            ui::report_error("runtime_ui_tools.hpp", message);
        }
        ImGui::EndDisabled();
    }
};
} // namespace forge
