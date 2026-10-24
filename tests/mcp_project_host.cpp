#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include "automation.hpp"
#include "mcp_test_marker.hpp"
#include "project_automation.hpp"
#include <chrono>
#include <iostream>
#include <thread>

int main(int argc, char** argv) {
    try {
        if (argc != 3)
            return 2;
        const auto root = std::filesystem::u8path(argv[1]);
#ifdef _WIN32
        const auto marker = root / "marker-sharing-probe";
        const auto handle = CreateFileW(marker.c_str(), GENERIC_READ | GENERIC_WRITE,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS,
                                        FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            throw std::runtime_error("Cannot create the native marker-sharing probe");
        const bool consumed = forge::test::consume_marker(marker);
        const bool present = forge::test::marker_present(marker);
        CloseHandle(handle);
        if (consumed || !present || !forge::test::consume_marker(marker))
            throw std::runtime_error("A held fixture marker was consumed or lost");
#endif
        const auto mode = std::string(argv[2]);
        const bool gui_recovery = mode == "recovery";
        forge::SceneDocument::create_project(root / "Initial", "Initial project");
        forge::SceneDocument::create_project(root / "Outside", "Outside project");
        std::filesystem::create_directory(root / "Projects");
        forge::EngineContext engine;
        forge::Scene scene(engine.world());
        std::vector<std::string> recent;
        forge::EditorFiles files(scene, nullptr, recent);
        files.start(root / "Initial");
        if (gui_recovery) {
            forge::authoring_command(scene, "entity.create",
                                     {{"kind", 0}, {"name", "Startup recovery cube"}});
            files.document.autosave();
            files.start(root / "Initial");
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = {1200, 800};
            io.DeltaTime = 1.f / 60;
            unsigned char* pixels;
            int width, height;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        }
        forge::ProjectSettingsEditor settings;
        files.automation_unavailable = [&](bool switching) {
            return (switching &&
                    (settings.dirty() || forge::test::marker_present(root / "source_draft"))) ||
                           forge::test::marker_present(root / "owner_busy")
                       ? "Settle pending project work"
                       : "";
        };
        forge::ui::AutomationWorkspace workspace;
        workspace.workflow_methods = [&](const auto& permissions) {
            auto methods = forge::ProjectAutomation::project_methods(files, permissions);
            auto values =
                forge::ProjectAutomation::settings_methods(settings, files.document, permissions);
            methods.insert(methods.end(), std::make_move_iterator(values.begin()),
                           std::make_move_iterator(values.end()));
            return methods;
        };
        forge::ui::AutomationPermissions permissions;
        permissions.assets = mode != "read";
        if (mode != "current")
            permissions.workspace_root = root / "Projects";
        workspace.start(scene, files, mode != "read", mode != "read", permissions);
        std::cout << workspace.connection().connection().dump() << std::endl;
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(150);
        while (workspace.active() && std::chrono::steady_clock::now() < end) {
            if (gui_recovery) {
                ImGui::NewFrame();
                files.draw_dialogs();
                if (forge::test::marker_present(root / "other_popup"))
                    ImGui::OpenPopup("Unrelated project fixture dialog");
                if (ImGui::BeginPopupModal("Unrelated project fixture dialog", nullptr,
                                           ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::TextUnformatted("Another owner is active");
                    if (!forge::test::marker_present(root / "other_popup"))
                        ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }
            }
            files.changed = false;
            if (forge::test::consume_marker(root / "autosave"))
                files.document.autosave();
            if (forge::test::consume_marker(root / "ui_outside"))
                files.document.open_project(root / "Outside");
            if (forge::test::consume_marker(root / "ui_inside"))
                files.document.open_project(root / "Projects/AgentGame");
            workspace.pump(files.document,
                           forge::test::marker_present(root / "gesture") ||
                                   (gui_recovery &&
                                    ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId |
                                                                    ImGuiPopupFlags_AnyPopupLevel))
                               ? "Active user interaction or recovery dialog"
                               : "");
            if (gui_recovery)
                ImGui::Render();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!workspace.active())
            std::cerr << "MCP owner connection stopped: " << workspace.diagnostic() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
