#include "automation.hpp"
#include "project_automation.hpp"
#include <chrono>
#include <iostream>
#include <thread>

int main(int argc, char** argv) {
    try {
        if (argc != 3)
            return 2;
        const auto root = std::filesystem::u8path(argv[1]);
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
                    (settings.dirty() || std::filesystem::exists(root / "source_draft"))) ||
                           std::filesystem::exists(root / "owner_busy")
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
                if (std::filesystem::exists(root / "other_popup"))
                    ImGui::OpenPopup("Unrelated project fixture dialog");
                if (ImGui::BeginPopupModal("Unrelated project fixture dialog", nullptr,
                                           ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::TextUnformatted("Another owner is active");
                    if (!std::filesystem::exists(root / "other_popup"))
                        ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }
            }
            files.changed = false;
            if (std::filesystem::remove(root / "autosave"))
                files.document.autosave();
            if (std::filesystem::remove(root / "ui_outside"))
                files.document.open_project(root / "Outside");
            if (std::filesystem::remove(root / "ui_inside"))
                files.document.open_project(root / "Projects/AgentGame");
            workspace.pump(files.document,
                           std::filesystem::exists(root / "gesture") ||
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
