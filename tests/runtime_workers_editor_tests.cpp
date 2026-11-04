#include "editor/project_settings.hpp"
#include "imgui_input_driver.hpp"
#include "imgui_software_capture.hpp"
#include <imgui_internal.h>
#include <iostream>
using namespace forge;
namespace {
void check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
void exercise(const std::filesystem::path& scratch, float scale, bool compact,
              const std::filesystem::path& captures) {
    const auto root = std::filesystem::absolute(scratch) / AssetId::generate().str() / "WorkerDemo";
    std::filesystem::create_directories(root.parent_path());
    SceneDocument::create_project(root, "WorkerDemo");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument document(scene);
    document.open_project(root, true);
    ProjectSettingsEditor settings;
    ui::EditorUiContext context;
    ui::ContextScope scope(context);
    const unsigned width = unsigned((compact ? 460 : 720) * scale), height = unsigned(720 * scale);
    test::ImGuiSoftwareCapture capture(width, height);
    ui::style(scale);
    ImGui::GetIO().ConfigInputTrickleEventQueue = false;
    test::observe_ui = true;
    std::string status;
    bool locked = false;
    settings.open();
    auto frame = [&] {
        test::ui_targets.clear();
        capture.new_frame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        if (auto* window = ImGui::FindWindowByName("Project Settings"))
            ImGui::SetWindowSize(window, {float(width), float(height)}, ImGuiCond_Always);
        settings.draw(document, scene, locked, status);
        capture.render();
    };
    test::ImGuiInputDriver input(frame);
    frame();
    frame();
    input.type("project-settings:ecs-workers", "4");
    check(settings.dirty(), "Actual worker control did not edit the project draft");
    settings.request_save();
    frame();
    check(!settings.dirty() && ProjectSettings(root).execution().ecs_workers == 4,
          "Worker control did not use the ordinary project Save owner");
    frame();
    if (!captures.empty() && scale == 1 && !compact) {
        std::filesystem::create_directories(captures);
        capture.save(captures / "fixed-ecs-workers-settings.png");
    }
    const auto saved = document.settings().document();
    input.type("project-settings:ecs-workers", "0");
    check(!settings.dirty() && ProjectSettings(root).document() == saved,
          "Invalid worker control changed saved or draft settings");
    input.type("project-settings:ecs-workers", "4");
    locked = true;
    frame();
    check(!test::ui_targets.at("project-settings:ecs-workers").enabled,
          "Worker control escaped the native Play/settings lock");
    locked = false;
    frame();
    test::observe_ui = false;
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2 || argc == 3, "Need scratch and optional capture directory");
        for (float scale : {1.f, 1.5f, 2.f})
            for (bool compact : {false, true})
                exercise(argv[1], scale, compact, argc == 3 ? argv[2] : "");
        std::cout
            << "Actual fixed-worker settings controls, save/validation/lock passed six layouts\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
