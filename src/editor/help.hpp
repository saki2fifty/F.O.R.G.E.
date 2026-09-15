#pragma once
#include "widgets.hpp"
#include <SDL3/SDL.h>
#include <filesystem>
#include <forge/build.hpp>
#include <string>
namespace forge::ui {
inline std::string local_file_url(const std::filesystem::path& path) {
    const auto text = std::filesystem::absolute(path).generic_u8string();
    std::string result = text.starts_with(u8"/") ? "file://" : "file:///";
    constexpr char hex[] = "0123456789ABCDEF";
    for (const auto value : text) {
        const auto byte = static_cast<unsigned char>(value);
        if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
            (byte >= '0' && byte <= '9') || byte == '/' || byte == ':' || byte == '-' ||
            byte == '_' || byte == '.' || byte == '~')
            result += static_cast<char>(byte);
        else {
            result += '%';
            result += hex[byte >> 4];
            result += hex[byte & 15];
        }
    }
    return result;
}
inline void help_menu(const std::filesystem::path& executable_folder, std::string& status) {
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("User Manual")) {
            try {
                const auto manual = executable_folder / "manual/index.html";
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
