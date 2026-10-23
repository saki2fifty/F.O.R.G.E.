#include "automation.hpp"
#include <chrono>
#include <iostream>
#include <thread>

// Real editor document/file owners without a GPU window. Control markers belong
// only to this test host; they are not product automation methods.
int main(int argc, char** argv) {
    try {
        if (argc != 3)
            return 2;
        const auto root = std::filesystem::u8path(argv[1]);
        const auto mode = std::string(argv[2]);
        forge::SceneDocument::create_project(root / "Game", "Agent Game");
        forge::SceneDocument::create_project(root / "Other", "Other");
        forge::EngineContext engine;
        forge::Scene scene(engine.world());
        std::vector<std::string> recent;
        forge::EditorFiles files(scene, nullptr, recent);
        files.start(root / "Game");
        files.automation_unavailable = [&](bool switching) {
            return std::filesystem::exists(root / "block_all") ||
                           (switching && std::filesystem::exists(root / "source_draft"))
                       ? "Test owner has pending work"
                       : "";
        };
        forge::ui::AutomationWorkspace workspace;
        workspace.start(scene, files, mode != "read", mode == "files");
        std::cout << workspace.connection().connection().dump() << std::endl;
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(90);
        while (workspace.active() && std::chrono::steady_clock::now() < end) {
            if (std::filesystem::remove(root / "autosave"))
                files.document.autosave();
            if (std::filesystem::remove(root / "ui_new"))
                files.document.new_scene();
            if (std::filesystem::remove(root / "ui_project"))
                files.document.open_project(root / "Other");
            files.changed = false; // Main loop consumes the completed scene-switch event.
            files.external_busy = [&] { return std::filesystem::exists(root / "file_busy"); };
            files.set_switch_available(!std::filesystem::exists(root / "build_busy"));
            workspace.pump(files.document,
                           std::filesystem::exists(root / "gesture") ? "Active test gesture" : "");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
