#pragma once
#include "automation.hpp"
#include <chrono>
void require(bool condition, const char* message);
inline void test_automation_workspace(float scale) {
    const auto root = std::filesystem::current_path() /
                      ("automation-ui-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(root);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    } cleanup{root};
    forge::SceneDocument::create_project(root / "Game", "Game");
    forge::SceneDocument::create_project(root / "Other", "Other");
    forge::EngineContext scene_engine;
    forge::Scene scene(scene_engine.world());
    forge::SceneDocument document(scene);
    document.open_project(root / "Game");
    forge::ui::AutomationWorkspace workspace;
    require(!workspace.connection().active(), "Automation enabled itself");
    workspace.start(scene, document, false);
    const auto old_connection = workspace.connection().connection();
    document.save();
    workspace.pump(document, "");
    require(workspace.connection().active(), "Ordinary Save revoked the document session");
    forge::ProjectLease other(root / "Other");
    bool rejected = false;
    const auto generation = document.generation();
    try {
        document.open_project(root / "Other");
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected && document.generation() == generation && document.name() == "Game",
            "Failed writer acquisition replaced the active project");
    workspace.pump(document, "");
    require(workspace.connection().active(), "Failed project switch revoked current session");
    document.save_as(root / "Game/Scenes/copy.scene.json");
    workspace.pump(document, "");
    require(!workspace.connection().active(), "Save As left old document access active");
    workspace.start(scene, document, true);
    require(workspace.connection().connection().at("token") != old_connection.at("token"),
            "Restart reused access token");
    require(workspace.connection().editable(), "Edit capability not enabled explicitly");
    document.new_scene();
    workspace.pump(document, "");
    require(!workspace.connection().active(), "New scene did not revoke automation");
    ImGui::CreateContext();
    forge::ui::style(scale);
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1400, 1000};
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    workspace.open = true;
    for (int mode = 0; mode < 3; ++mode) {
        if (mode)
            workspace.start(scene, document, mode == 2);
        for (int frame = 0; frame < 3; ++frame) {
            ImGui::NewFrame();
            workspace.draw(scene, document, mode == 2 ? "Active gesture" : "");
            ImGui::Render();
        }
        const auto* window = ImGui::FindWindowByName("Local automation");
        require(window && window->Active, "Automation controls missing at supported scale");
    }
    ImGui::DestroyContext();
}
