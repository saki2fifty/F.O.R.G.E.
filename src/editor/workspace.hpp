#pragma once
#include "widgets.hpp"
#include <cstdio>
#include <forge/scene.hpp>
#include <fstream>
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
         {std::pair{"World", "Hierarchy###World"}, std::pair{"Native", "Gameplay Code###Native"},
          std::pair{"Prefab source", "Prefab source###Prefab source"},
          std::pair{"Project Settings", "Project Settings###Project Settings"}}) {
        replace(std::string("[Window][") + names.first + "]",
                std::string("[Window][") + names.second + "]");
        char old_id[16], new_id[16];
        std::snprintf(old_id, sizeof(old_id), "0x%08X", ImHashStr(names.first));
        std::snprintf(new_id, sizeof(new_id), "0x%08X", ImHashStr(names.second));
        replace(old_id, new_id);
    }
    return text;
}
struct StartupLayout {
    std::string text, warning;
    bool save_enabled = true;
};
inline StartupLayout prepare_layout(const std::filesystem::path& path) {
    StartupLayout result;
    try {
        if (!std::filesystem::exists(path))
            return result;
        std::string original;
        {
            // Close the reader before atomic replacement: Windows readers may deny deletion.
            std::ifstream input(path, std::ios::binary);
            if (!input)
                throw std::runtime_error("Cannot read workspace layout");
            original.assign(std::istreambuf_iterator<char>(input), {});
            if (input.bad())
                throw std::runtime_error("Cannot finish reading workspace layout");
        }
        result.text = migrate_layout(original);
        if (result.text != original) {
            const auto backup = path.parent_path() / "workspace-before-layout-update.ini";
            if (!std::filesystem::exists(backup))
                atomic_write(backup, original);
            else if (!std::filesystem::is_regular_file(backup))
                throw std::runtime_error("Workspace backup path is not a file");
            // An existing backup (including one left by Build 12) is retained.
            atomic_write(path, result.text);
        }
    } catch (const std::exception& error) {
        result.save_enabled = false;
        const auto name = path.u8string();
        result.warning =
            "Workspace layout could not be prepared: " + std::string(name.begin(), name.end()) +
            ". " + error.what() +
            ". Layout changes will not be saved this session; the original file is preserved.";
    }
    return result;
}
inline void load_startup_layout(const StartupLayout& layout, const char* path) {
    ImGui::GetIO().IniFilename = layout.save_enabled ? path : nullptr;
    if (!layout.text.empty())
        ImGui::LoadIniSettingsFromMemory(layout.text.data(), layout.text.size());
}
struct Workspace {
    bool hierarchy = true, inspector = true, scene = true, content = true, console = true,
         build = true, game = true, problems = true;
    bool reset = false;
    void load(const Json& settings) {
        const auto p = settings.value("panels", Json::object());
        game = p.value("game", true);
        problems = p.value("problems", true);
        hierarchy = p.value("hierarchy", true);
        inspector = p.value("inspector", true);
        scene = p.value("scene", true);
        content = p.value("content", true);
        console = p.value("console", true);
        build = p.value("build", true);
    }
    Json settings() const {
        return {{"hierarchy", hierarchy}, {"inspector", inspector}, {"scene", scene},
                {"content", content},     {"console", console},     {"build", build},
                {"game", game},           {"problems", problems}};
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
                           {"Game", &game},
                           {"Problems", &problems},
                           {"Content", &content},
                           {"Console", &console},
                           {"Gameplay Code", &build}}) {
                changed |= ImGui::MenuItem(p.name, nullptr, p.value);
                help(
                    "Show or hide this panel. Dock arrangements are saved when the editor closes.");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Reset layout")) {
                hierarchy = inspector = scene = game = content = problems = console = build = true;
                reset = true;
                changed = true;
            }
            help("Restore Hierarchy left, Scene/Game center, Inspector right, and "
                 "Content/Problems/Console/Gameplay Code tabs below. Replaces your custom dock "
                 "arrangement.");
            ImGui::EndMenu();
        }
        help("Recover hidden panels or restore the default workspace.");
        return changed;
    }
};
} // namespace forge::ui
