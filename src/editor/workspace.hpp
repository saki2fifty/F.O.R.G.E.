#pragma once
#include "widgets.hpp"
#include <cstdio>
#include <forge/scene.hpp>
namespace forge::ui {
// Migrate window sections and docking references together; preserve custom geometry.
inline std::string migrate_layout(std::string text) {
    auto replace = [&](const std::string& before, const std::string& after) {
        std::size_t pos = 0;
        while ((pos = text.find(before, pos)) != std::string::npos) {
            text.replace(pos, before.size(), after);
            pos += after.size();
        }
    };
    for (const auto& names :
         {std::pair{"World", "Hierarchy###World"}, std::pair{"Native", "Gameplay Code###Native"}}) {
        replace(std::string("[Window][") + names.first + "]",
                std::string("[Window][") + names.second + "]");
        char old_id[16], new_id[16];
        std::snprintf(old_id, sizeof(old_id), "0x%08X", ImHashStr(names.first));
        std::snprintf(new_id, sizeof(new_id), "0x%08X", ImHashStr(names.second));
        replace(old_id, new_id);
    }
    return text;
}
struct Workspace {
    bool hierarchy = true, inspector = true, scene = true, content = true, console = true,
         build = true;
    bool reset = false;
    void load(const Json& settings) {
        const auto p = settings.value("panels", Json::object());
        hierarchy = p.value("hierarchy", true);
        inspector = p.value("inspector", true);
        scene = p.value("scene", true);
        content = p.value("content", true);
        console = p.value("console", true);
        build = p.value("build", true);
    }
    Json settings() const {
        return {{"hierarchy", hierarchy}, {"inspector", inspector}, {"scene", scene},
                {"content", content},     {"console", console},     {"build", build}};
    }
    bool menu() {
        bool changed = false;
        if (ImGui::BeginMenu("Window")) {
            struct Panel {
                const char* name;
                bool* value;
            };
            for (auto p : {Panel{"Hierarchy", &hierarchy},
                           {"Inspector", &inspector},
                           {"Scene", &scene},
                           {"Content", &content},
                           {"Console", &console},
                           {"Gameplay Code", &build}}) {
                changed |= ImGui::MenuItem(p.name, nullptr, p.value);
                help(
                    "Show or hide this panel. Dock arrangements are saved when the editor closes.");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Reset layout")) {
                hierarchy = inspector = scene = content = console = build = true;
                reset = true;
                changed = true;
            }
            help(
                "Restore Hierarchy left, Scene center, Inspector right, and "
                "Content/Console/Gameplay Code tabs below. Replaces your custom dock arrangement.");
            ImGui::EndMenu();
        }
        help("Recover hidden panels or restore the default workspace.");
        return changed;
    }
};
} // namespace forge::ui
