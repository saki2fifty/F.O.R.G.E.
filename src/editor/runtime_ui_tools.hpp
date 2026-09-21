#pragma once
#include "../ui_asset_catalog.hpp"
#include "document.hpp"
#include "property_drawer.hpp"
#include <forge/authoring.hpp>
#include <forge/ui_assets.hpp>
namespace forge {
class RuntimeUiTools {
    char source_[1024]{};

  public:
    void content(SceneDocument& project, bool locked, std::string& message,
                 const UiAssetSnapshot* observed = nullptr) {
        if (!ImGui::CollapsingHeader("Runtime UI")) {
            ui::help(
                "Create a game HUD example or register project RML, styles, fonts and images.");
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
            ImGui::InputText("Project UI file", source_, sizeof(source_));
            ui::help("Project-relative RML, RCSS, TTF/OTF or TGA. Registration records admitted "
                     "source metadata; it does not replace live UI or its font cache.");
            if (ui::button("Register / Refresh",
                           "Admit this source and update its catalog metadata, preserving an "
                           "existing identity. Assign RML using UI Document in Inspector.")) {
                project.check_ownership();
                auto record =
                    register_ui_source(*project.writer_guard(), std::filesystem::u8path(source_));
                message = "UI source registered: " + path_utf8(record.source);
            }
            ImGui::BeginDisabled(!observed || observed->documents.empty());
            if (ui::button("Refresh loaded resources",
                           "After testing the HUD in Play, stop and record the files admitted by "
                           "its last successful load. Changed files reject this refresh; reload "
                           "the HUD first. Does not alter scene Undo or live fonts.")) {
                project.check_ownership();
                (void)refresh_ui_asset_catalog(*project.writer_guard(), *observed);
                message = "UI source metadata and observed dependencies refreshed.";
            }
            ImGui::EndDisabled();
        } catch (const std::exception& e) {
            message = e.what();
            ui::report_error("runtime_ui_tools.hpp", message);
        }
        ImGui::EndDisabled();
    }
};
} // namespace forge
