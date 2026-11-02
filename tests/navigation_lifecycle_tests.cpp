#include "editor/camera.hpp"
#include "editor/navigation_automation.hpp"
#include "editor/spatial_helpers.hpp"
#include <imgui_internal.h>
#include <iostream>
#include <thread>

using namespace forge;
namespace {
void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
} // namespace

#include "navigation_destination_gesture_tests.hpp"
#include "navigation_destination_tests.hpp"
#include "navigation_region_gesture_tests.hpp"
#include "navigation_region_helpers_tests.hpp"
#include "navigation_region_input_tests.hpp"
void planar_editor(const std::filesystem::path& root, const std::filesystem::path& worker) {
    SceneDocument::create_project(root, "Planar navigation");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root);
    const auto region =
        authoring_command(scene, "entity.create", {{"kind", 4}, {"name", "Walkable region"}})
            .at("selected")
            .get<std::string>();
    authoring_command(scene, "component.add",
                      {{"entity", region}, {"component", "forge.navigation_region_2d"}});
    project.save();
    NavigationSettings settings;
    settings.plane = NavigationPlane::PlanarXY;
    NavigationTools tools(engine.world(), worker);
    tools.build(scene, project, settings);
    std::string message;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (tools.pending()) {
        tools.poll(scene, project, false, message);
        check(std::chrono::steady_clock::now() < deadline, "Planar editor bake timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(message.find("failed") == std::string::npos, "Planar editor bake failed");
    auto wide = settings;
    wide.profile = "Wide_agent";
    wide.radius = .8f;
    tools.build(scene, project, wide);
    const auto wide_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (tools.pending()) {
        tools.poll(scene, project, false, message);
        check(std::chrono::steady_clock::now() < wide_deadline, "Named profile bake timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(message.find("failed") == std::string::npos, "Named profile bake failed");
    const auto saved = scene.document();
    const bool had_undo = scene.can_undo();
    const auto catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
    check(navigation_profiles(*catalog, scene.asset_id()).size() == 2,
          "Editor build replaced the default profile");
    for (const auto& [name, id] : navigation_profiles(*catalog, scene.asset_id()))
        check(
            content_member_name(catalog->records().at(id)) == "Navigation / " + name &&
                asset_display(catalog->records().at(id)).starts_with("Navigation / " + name + " ("),
            "Content and NavMesh picker labels omit the profile name");
    tools.select_profile(scene, project, *catalog, "default");
    for (const float scale : {1.f, 1.5f, 2.f}) {
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1400, 1000};
        io.DeltaTime = 1.f / 60;
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        ui::style(scale);
        check_region_helpers(scene, region, scale);
        ui::EditorUiContext context;
        ui::ContextScope scope(context);
        test::observe_ui = true;
        EditorCamera camera;
        ui::SpatialHelpers helpers;
        std::uint64_t helper_generation = 0;
        camera.align(2, 1);
        camera.distance = 20;
        const auto frame = [&](bool locked = false) {
            test::ui_targets.clear();
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
            ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
            ImGui::Begin("XY navigation authoring");
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);
            tools.content(scene, project, locked, message, catalog);
            const auto source = scene.effective_document();
            helpers.update(source, ++helper_generation, 1400, 1000);
            helpers.draw(camera, region, {0, 0}, {1400, 1000}, 0, tools.overlay_visible());
            tools.draw(source, camera, {0, 0}, {1400, 1000});
            ImGui::End();
            ImGui::Render();
        };
        frame();
        frame();
        check(test::ui_targets.contains("navigation:status:NavMesh ready") &&
                  test::ui_targets.contains("navigation:region-guide") &&
                  test::ui_targets.contains("navigation:setting:Agent radius") &&
                  !test::ui_targets.contains("navigation:setting:Agent height"),
              "Actual XY UI did not load saved plane, region guide or relevant controls");
        const auto click = [&](const std::string& key) {
            const auto box = test::ui_targets.at(key);
            io.AddMousePosEvent((box.minimum.x + box.maximum.x) * .5f,
                                (box.minimum.y + box.maximum.y) * .5f);
            frame();
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            frame();
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            frame();
        };
        click("navigation:profile");
        frame();
        click("navigation:profile:Wide_agent");
        frame();
        check(test::ui_targets.contains("navigation:profile-selected:Wide_agent") &&
                  test::ui_targets.contains("navigation:status:NavMesh ready"),
              "Actual profile selection did not load the saved named settings and asset");
        click("navigation:setting:Agent radius");
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        frame();
        io.AddKeyEvent(ImGuiKey_A, true);
        frame();
        io.AddKeyEvent(ImGuiKey_A, false);
        frame();
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        frame();
        io.AddInputCharactersUTF8("999");
        frame();
        io.AddKeyEvent(ImGuiKey_Enter, true);
        frame();
        io.AddKeyEvent(ImGuiKey_Enter, false);
        frame();
        check(test::ui_targets.contains(
                  "navigation:status:Navigation is stale — rebuild before Play"),
              "Actual invalid numeric input did not modify the profile draft");
        click("navigation:profile");
        frame();
        click("navigation:profile:default");
        frame();
        check(test::ui_targets.contains("navigation:profile-selected:default") &&
                  test::ui_targets.contains("navigation:status:NavMesh ready"),
              "Default profile selection did not restore saved readiness");
        if (scale == 1) {
            click("navigation:profile");
            frame();
            click("navigation:profile-new");
            frame();
            frame();
            click("navigation:new-profile");
            io.AddInputCharactersUTF8("LocalTest");
            frame();
            click("button:Use new profile");
            frame();
            check(test::ui_targets.contains("navigation:profile-selected:LocalTest") &&
                      test::ui_targets.contains(
                          "navigation:status:No NavMesh built for this profile"),
                  "Actual new profile input did not create an unpublished settings draft");
            click("navigation:profile");
            frame();
            click("navigation:profile:default");
            frame();
        }
        click("navigation:setting:Plane");
        frame();
        check(test::ui_targets.contains("navigation:plane:0"), "Plane selector did not open");
        click("navigation:plane:0");
        frame();
        check(test::ui_targets.contains("navigation:setting:Agent height") &&
                  test::ui_targets.contains(
                      "navigation:status:Navigation is stale — rebuild before Play"),
              "Actual plane selection did not change controls and mark the draft stale");
        click("navigation:setting:Plane");
        frame();
        click("navigation:plane:1");
        frame();
        check(test::ui_targets.contains("navigation:status:NavMesh ready") &&
                  !test::ui_targets.contains("navigation:setting:Agent height"),
              "Selecting XY again did not restore saved readiness");
        frame(true);
        check(!test::ui_targets.at("navigation:profile").enabled &&
                  !test::ui_targets.at("navigation:setting:Plane").enabled &&
                  !test::ui_targets.at("button:Build NavMesh").enabled,
              "Play lock allowed changing or building the navigation profile");
        check(scene.document() == saved && scene.can_undo() == had_undo &&
                  AssetCatalog::open_project(root).document() == catalog->document(),
              "Navigation presentation/draft controls changed authored state or catalog");
        check_region_resize_input(scene, region, scale);
        if (scale == 2)
            check_region_resize_input(scene, region, scale, true);
        test::observe_ui = false;
        ImGui::DestroyContext();
    }
    std::cout << "Actual XY plane selection/guide/ready-stale/Play lock at 100/150/200% passed\n";
}

int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need navigation lifecycle scratch root and actual worker");
        const auto root = std::filesystem::absolute(argv[1]);
        check_destination_gestures(root);
        check_region_gestures(root);
        const auto first = root / "First", second = root / "Second";
        SceneDocument::create_project(first, "First");
        SceneDocument::create_project(second, "Second");
        EngineContext engine;
        Scene scene(engine.world());
        std::vector<std::string> recent;
        EditorFiles files(scene, nullptr, recent);
        files.start(first);
        const auto floor =
            authoring_command(scene, "entity.create", {{"kind", 0}, {"name", "Floor"}})
                .at("selected")
                .get<std::string>();
        scene.entity(floor).set<LocalScale>({20, 1, 20});
        scene.entity(floor).set<LocalTranslation>({0, -.5, 0});
        scene.entity(floor).set<NavigationSurface>({true});
        files.document.save();
        // Copying a project preserves its scene UUID. The second project has no
        // baked navigation, despite containing the same authored Scene identity.
        const auto scene_path = files.document.path().lexically_relative(first);
        std::filesystem::copy_file(first / scene_path, second / scene_path,
                                   std::filesystem::copy_options::overwrite_existing);
        std::filesystem::copy_file(first / "forge.project.json", second / "forge.project.json",
                                   std::filesystem::copy_options::overwrite_existing);
        const auto saved_scene = scene.document();
        NavigationTools navigation(engine.world(), std::filesystem::absolute(argv[2]));
        navigation.build(scene, files.document, {});
        std::string message;
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (navigation.pending()) {
            navigation.poll(scene, files.document, false, message);
            check(std::chrono::steady_clock::now() < until, "Navigation worker did not settle");
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        const auto baked = NavigationAutomation::state(navigation, scene, files);
        check(baked.at("error") == "" && !baked.at("asset").is_null(),
              "Actual first-project navigation build failed");
        const auto saved_catalog = AssetCatalog::open_project(first).document();

        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1200, 900};
        io.DeltaTime = 1.f / 60;
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        EditorCamera camera;
        camera.target = {0, 0, 0};
        camera.pitch = -.7f;
        camera.distance = 30;
        const auto overlay_vertices = [&](bool load_saved = false) {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
            ImGui::SetNextWindowSize({1200, 900}, ImGuiCond_Always);
            ImGui::Begin("Navigation overlay boundary");
            if (load_saved) {
                ImGui::SetNextItemOpen(true, ImGuiCond_Always);
                navigation.content(scene, files.document, false, message,
                                   std::make_shared<const AssetCatalog>(
                                       AssetCatalog::open_project(files.document.project())));
            }
            auto* draw = ImGui::GetWindowDrawList();
            const auto before = draw->VtxBuffer.Size;
            navigation.draw(scene.effective_document(), camera, {0, 0}, {1200, 900});
            const auto added = draw->VtxBuffer.Size - before;
            ImGui::End();
            ImGui::Render();
            return added;
        };
        const auto original_vertices = overlay_vertices();
        check(original_vertices > 0, "Baked navigation did not draw in its original project");
        navigation.poll(scene, files.document, false, message);
        check(overlay_vertices() == original_vertices,
              "Idle same-project update discarded the usable navigation overlay");
        files.automation_project({EditorFiles::Command::OpenProject, second, {}}, false);
        check(scene.document() == saved_scene, "Copied project did not preserve the Scene");
        navigation.poll(scene, files.document, false, message);
        check(!NavigationAutomation::state(navigation, scene, files).at("started").get<bool>(),
              "Navigation adapter exposed another project's bake");
        check(overlay_vertices() == 0,
              "Old navigation overlay remained in a different project with the same Scene UUID");
        files.changed = false; // Consume the file-change event as the editor frame does.
        files.automation_project({EditorFiles::Command::OpenProject, first, {}}, false);
        navigation.poll(scene, files.document, false, message);
        check(overlay_vertices() == 0, "Returning to a project resurrected a stale cache");
        check(overlay_vertices(true) == original_vertices && !navigation.pending(),
              "Opening Navigation did not reload the saved overlay without rebuilding");
        check(NavigationAutomation::state(navigation, scene, files).at("asset") ==
                  baked.at("asset"),
              "Reopening Navigation lost the saved asset selection");
        check(scene.document() == saved_scene && !scene.can_undo() &&
                  AssetCatalog::open_project(first).document() == saved_catalog &&
                  AssetCatalog::open_project(second).records().empty(),
              "Navigation cache changes wrote authored state or either project catalog");
        files.changed = false;
        files.automation_switch({EditorFiles::Command::NewScene, {}, {}}, false);
        navigation.poll(scene, files.document, false, message);
        check(overlay_vertices() == 0 &&
                  !NavigationAutomation::state(navigation, scene, files).at("started").get<bool>(),
              "Scene switching retained the previous scene's navigation cache");
        ImGui::DestroyContext();
        planar_editor(root / "Planar", std::filesystem::absolute(argv[2]));
        check_navigation_destinations(root / "GoalsXY", std::filesystem::absolute(argv[2]),
                                      NavigationPlane::PlanarXY);
        check_navigation_destinations(root / "Goals3D", std::filesystem::absolute(argv[2]),
                                      NavigationPlane::Spatial3D);
        std::cout << "Actual baked navigation overlay respects project/scene boundaries and saved "
                     "reload\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
