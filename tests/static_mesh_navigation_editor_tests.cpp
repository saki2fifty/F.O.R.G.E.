#include "navigation_tools.hpp"
#include <imgui_internal.h>
#include <iostream>
using namespace forge;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
void exercise(const std::filesystem::path& root, float scale, ImVec2 size) {
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root);
    project.open_scene("Scenes/51-static-mesh-navigation.scene.json");
    const auto saved = scene.document();
    const auto catalog = AssetCatalog::open_project(root).document();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = size;
    io.DeltaTime = 1.f / 60;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ui::style(scale);
    ui::EditorUiContext context;
    ui::ContextScope scope(context);
    test::observe_ui = true;
    NavigationTools tools(root / "unused-worker");
    std::string message;
    auto frame = [&](bool locked) {
        test::ui_targets.clear();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::Begin("Static Mesh navigation");
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        tools.content(scene, project, locked, message);
        tools.poll(scene, project, locked, message);
        ImGui::End();
        ImGui::Render();
        check(message.empty(), "Navigation controls reported an error");
    };
    frame(false);
    frame(false);
    check(test::ui_targets.contains("navigation:status:NavMesh ready"),
          "New Mesh navmesh not recognized as ready by actual UI");
    check(test::ui_targets.contains("button:Build NavMesh") &&
              test::ui_targets.at("button:Build NavMesh").enabled,
          "Static Mesh bake action missing/disabled");
    frame(true);
    check(!test::ui_targets.at("button:Build NavMesh").enabled,
          "Play lock allowed navigation publication");
    check(scene.document() == saved && AssetCatalog::open_project(root).document() == catalog &&
              !scene.can_undo(),
          "Readonly navigation controls changed authored state or catalog");
    std::string floor;
    for (const auto& row : saved.at("entities"))
        if (row.at("name") == "Editable Mesh floor - rebuild navigation after editing")
            floor = row.at("id");
    check(!floor.empty(), "Gallery51 floor missing");
    authoring_command(scene, "property.set",
                      {{"entity", floor},
                       {"component", "forge.local_translation"},
                       {"field", "x"},
                       {"value", 1}});
    frame(false);
    check(test::ui_targets.contains("navigation:status:Navigation is stale — rebuild before Play"),
          "Scene revision did not refresh cached UI staleness immediately");
    scene.undo();
    frame(false);
    check(test::ui_targets.contains("navigation:status:NavMesh ready"),
          "Undo did not restore ready Mesh navigation status");
    project.open_scene("Scenes/50-navigation-progress.scene.json");
    frame(false);
    check(test::ui_targets.contains("navigation:status:NavMesh ready"),
          "Retained Nav1 Gallery did not remain ready in the version-aware UI");
    test::observe_ui = false;
    ImGui::DestroyContext();
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need Gallery and scratch root");
        const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        for (auto scale : {1.f, 1.5f, 2.f})
            for (auto size : {ImVec2{420, 640}, ImVec2{1200, 900}})
                exercise(root, scale, size);
        std::filesystem::remove_all(root);
        std::cout << "Static Mesh navigation actual UI ready/stale/Undo/Play-lock passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
