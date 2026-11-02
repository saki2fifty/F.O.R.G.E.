#include "component_inspector.hpp"
#include "imgui_software_capture.hpp"
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
    std::shared_ptr<const AssetCatalog> selection =
        std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
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
    NavigationTools tools(engine.world(), root / "unused-worker");
    std::string message;
    auto frame = [&](bool locked) {
        test::ui_targets.clear();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::Begin("Static Mesh navigation");
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        tools.content(scene, project, locked, message, selection);
        tools.poll(scene, project, locked, message);
        ImGui::End();
        ImGui::Render();
        check(message.empty(), "Navigation controls reported an error");
    };
    frame(false);
    frame(false);
    check(engine.services().profiles().empty(), "Disabled status profiling collected samples");
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
    const auto toggle_dynamic = [&] {
        const auto box = test::ui_targets.at("navigation:setting:Dynamic obstacles");
        check(box.minimum.y >= box.clip_minimum.y && box.maximum.y <= box.clip_maximum.y,
              "Dynamic-profile checkbox clips at a supported UI scale");
        io.AddMousePosEvent(box.minimum.x + 8, (box.minimum.y + box.maximum.y) * .5f);
        frame(false);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        frame(false);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        frame(false);
    };
    check(!test::ui_targets.at("navigation:setting:Dynamic obstacles").enabled,
          "Play lock allowed dynamic-profile changes");
    frame(false);
    toggle_dynamic();
    check(test::ui_targets.contains("navigation:status:Navigation is stale — rebuild before Play"),
          "Dynamic-profile choice did not require an explicit rebuild");
    selection = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
    frame(false);
    check(test::ui_targets.contains("navigation:status:Navigation is stale — rebuild before Play"),
          "Catalog refresh erased the dynamic-profile draft");
    toggle_dynamic();
    check(test::ui_targets.contains("navigation:status:NavMesh ready"),
          "Reverting the dynamic-profile choice failed to restore static readiness");
    check(scene.document() == saved && AssetCatalog::open_project(root).document() == catalog,
          "Editing the bake profile changed Scene history or published assets");
    auto radius = [&](const char* text) {
        const auto box = test::ui_targets.at("navigation:setting:Agent radius");
        const auto x = box.minimum.x + 40;
        const auto y = (box.minimum.y + box.maximum.y) * .5f;
        io.AddMousePosEvent(x, y);
        frame(false);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        frame(false);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        frame(false);
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        io.AddKeyEvent(ImGuiKey_LeftCtrl, true);
        io.AddKeyEvent(ImGuiKey_A, true);
        frame(false);
        io.AddKeyEvent(ImGuiKey_A, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        io.AddKeyEvent(ImGuiKey_LeftCtrl, false);
        frame(false);
        io.AddInputCharactersUTF8(text);
        frame(false);
        io.AddKeyEvent(ImGuiKey_Enter, true);
        frame(false);
        io.AddKeyEvent(ImGuiKey_Enter, false);
        frame(false);
    };
    radius("0.6");
    check(test::ui_targets.contains("navigation:status:Navigation is stale — rebuild before Play"),
          "Actual setting edit failed to invalidate navigation");
    selection = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
    frame(false);
    check(test::ui_targets.contains("navigation:status:Navigation is stale — rebuild before Play"),
          "Catalog snapshot replacement erased authored navigation settings draft");
    radius("0.4");
    check(test::ui_targets.contains("navigation:status:NavMesh ready"),
          "Actual setting revert failed to restore readiness");
    std::string floor;
    for (const auto& row : saved.at("entities"))
        if (row.at("name") == "Editable Mesh floor - rebuild navigation after editing")
            floor = row.at("id");
    check(!floor.empty(), "Gallery51 floor missing");
    engine.services().profiling(true);
    authoring_command(scene, "property.set",
                      {{"entity", floor},
                       {"component", "forge.local_translation"},
                       {"field", "x"},
                       {"value", 1}});
    frame(false);
#ifndef FORGE_DISABLE_PROFILING
    unsigned status_samples = 0;
    for (const auto& sample : engine.services().profiles())
        if (sample.at("name") == "NavigationStatus") {
            ++status_samples;
            check(sample.at("category") == "editor" && sample.at("tick") == 0 &&
                      sample.at("seconds").get<double>() >= 0,
                  "Invalid actual navigation status profile");
        }
    check(status_samples == 1, "Editor status did not record its validation scope");
#endif
    engine.services().profiling(false);
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
void capture_controls(const std::filesystem::path& root, const std::filesystem::path& output) {
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root);
    project.open_scene("Scenes/129-moving-navigation-3d.scene.json");
    std::string obstacle;
    const auto effective = scene.effective_document();
    for (const auto& row : effective.at("entities"))
        if (row.at("components").contains("forge.navigation_obstacle"))
            obstacle = row.at("id").get<std::string>();
    check(!obstacle.empty(), "Missing authored obstacle capture participant");
    const auto saved = scene.document();
    const auto catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
    test::ImGuiSoftwareCapture capture(1180, 760);
    ui::style(1);
    ui::EditorUiContext ui_context;
    ui::ContextScope context_scope(ui_context);
    ComponentInspector inspector;
    inspector.fixture_focus_component = "forge.navigation_obstacle";
    NavigationTools tools(engine.world(), root / "unused-worker");
    std::string message;
    test::observe_ui = true;
    for (unsigned frame = 0; frame < 6; ++frame) {
        test::ui_targets.clear();
        if (frame == 2 || frame == 3) {
            // Collapse the unrelated animator through real input for a focused capture.
            ImGui::GetIO().AddMousePosEvent(150, 115);
            ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, frame == 2);
        }
        capture.new_frame();
        ImGui::SetNextWindowPos({10, 10});
        ImGui::SetNextWindowSize({570, 740});
        ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        inspector.draw(scene, project, obstacle);
        ImGui::End();
        ImGui::SetNextWindowPos({590, 10});
        ImGui::SetNextWindowSize({580, 740});
        ImGui::Begin("Navigation", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        tools.content(scene, project, false, message, catalog);
        ImGui::End();
        capture.render();
    }
    check(message.empty() && test::ui_targets.contains("navigation:status:NavMesh ready") &&
              scene.document() == saved,
          "Native navigation capture changed its authored fixture or was not ready");
    capture.save(output / "navigation-dynamic-controls.png");
    test::observe_ui = false;
}
void capture_profiles(const std::filesystem::path& root, const std::filesystem::path& output) {
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root);
    project.open_scene("Scenes/138-navigation-profiles.scene.json");
    std::string actor;
    const auto effective = scene.effective_document();
    for (const auto& row : effective.at("entities"))
        if (row.at("name") == "Amber - Large profile left room")
            actor = row.at("id").get<std::string>();
    check(!actor.empty(), "Missing named-profile capture agent");
    const auto saved = scene.document();
    const auto catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
    const auto prior_catalog = catalog->document();
    test::ImGuiSoftwareCapture capture(1180, 760);
    ui::style(1);
    ui::EditorUiContext ui_context;
    ui::ContextScope scope(ui_context);
    ComponentInspector inspector;
    inspector.fixture_focus_component = "forge.navigation_agent";
    NavigationTools tools(engine.world(), root / "unused-worker");
    std::string message;
    test::observe_ui = true;
    const auto frame = [&] {
        test::ui_targets.clear();
        capture.new_frame();
        ImGui::SetNextWindowPos({10, 10});
        ImGui::SetNextWindowSize({570, 740});
        ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        inspector.draw(scene, project, actor);
        ImGui::End();
        ImGui::SetNextWindowPos({590, 10});
        ImGui::SetNextWindowSize({580, 740});
        ImGui::Begin("Navigation", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        tools.content(scene, project, false, message, catalog);
        ImGui::End();
        capture.render();
    };
    const auto click = [&](const std::string& key) {
        const auto target = test::ui_targets.at(key);
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent((target.minimum.x + target.maximum.x) * .5f,
                            (target.minimum.y + target.maximum.y) * .5f);
        frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        frame();
    };
    frame();
    frame();
    click("navigation:profile");
    frame();
    frame();
    click("navigation:profile:Large");
    frame();
    frame();
    check(message.empty() && test::ui_targets.contains("navigation:profile-selected:Large") &&
              test::ui_targets.contains("navigation:status:NavMesh ready") &&
              scene.document() == saved &&
              AssetCatalog::open_project(root).document() == prior_catalog,
          "Named-profile capture did not select the real saved view or changed authored data");
    capture.save(output / "navigation-profile-controls.png");
    test::observe_ui = false;
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3 || (argc == 4 && (std::string_view(argv[3]) == "--capture" ||
                                          std::string_view(argv[3]) == "--capture-profiles" ||
                                          std::string_view(argv[3]) == "--capture-all")),
              "Need Gallery, scratch root and optional --capture");
        const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        for (auto scale : {1.f, 1.5f, 2.f})
            for (auto size : {ImVec2{420, 640}, ImVec2{1200, 900}})
                exercise(root, scale, size);
        if (argc == 4) {
            if (std::string_view(argv[3]) != "--capture")
                capture_profiles(root, std::filesystem::absolute(argv[2]));
            if (std::string_view(argv[3]) != "--capture-profiles")
                capture_controls(root, std::filesystem::absolute(argv[2]));
        }
        std::filesystem::remove_all(root);
        std::cout << "Static Mesh navigation actual UI ready/stale/Undo/Play-lock passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
