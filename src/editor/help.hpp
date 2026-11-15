#pragma once
#include "../install_paths.hpp"
#include "../local_file_url.hpp"
#include "widgets.hpp"
#include <SDL3/SDL.h>
#include <filesystem>
#include <forge/build.hpp>
#include <string>
namespace forge::ui {
using ::forge::local_file_url;
inline void help_menu(const std::filesystem::path& executable_folder, std::string& status) {
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("User Manual")) {
            try {
                const auto manual = installation_root(executable_folder) / "manual/index.html";
                if (!std::filesystem::is_regular_file(manual))
                    status = "Manual missing. Extract the complete Windows ZIP beside the editor.";
                else if (!SDL_OpenURL(local_file_url(manual).c_str()))
                    status = std::string("Cannot open manual: ") + SDL_GetError();
                else
                    status = "Requested User Manual in your default browser";
            } catch (const std::exception& e) {
                status = e.what();
            }
        }
        help("Open the offline manual bundled with this build in your default browser.");
        ImGui::Separator();
        ImGui::Text("Build: %s", forge::build_id);
        help("UTC date and a globally increasing build counter. The counter never resets.");
        if (ImGui::MenuItem("Copy build information")) {
            const auto info = std::string("FORGE | Build: ") + forge::build_id +
                              "\nSource: " + forge::source_commit;
            if (!SDL_SetClipboardText(info.c_str()))
                status = SDL_GetError();
        }
        help("Copy the build identifier and source commit when reporting a problem.");
        ImGui::EndMenu();
    }
    help("Read the editor manual and identify this build.");
}
} // namespace forge::ui
