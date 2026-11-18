#include "editor/component_inspector.hpp"
#include "editor/navigation_automation.hpp"
#include "imgui_software_capture.hpp"
#include <imgui_internal.h>
#include <iostream>
#include <optional>
#include <set>
#include <thread>
using namespace forge;
namespace {
void check(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
struct Ui {
    Ui(float scale, ImVec2 size) {
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = size;
        io.DeltaTime = 1.f / 60;
        io.ConfigInputTrickleEventQueue = false;
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        ui::style(scale);
        test::observe_ui = true;
    }
    ~Ui() {
        test::observe_ui = false;
        ImGui::DestroyContext();
    }
    template <class Draw> void frame(Draw draw) {
        test::ui_targets.clear();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
        ImGui::Begin("Navigation link controls");
        draw();
        ImGui::End();
        ImGui::Render();
    }
};
template <class Frame> void click(Frame frame, const std::string& key) {
    frame();
    const auto box = test::ui_targets.at(key);
    check(box.enabled && box.minimum.x >= box.clip_minimum.x &&
              box.maximum.x <= box.clip_maximum.x && box.minimum.y >= box.clip_minimum.y &&
              box.maximum.y <= box.clip_maximum.y,
          "Link control is disabled or clipped: " + key);
    auto& io = ImGui::GetIO();
    io.AddMousePosEvent((box.minimum.x + box.maximum.x) * .5f,
                        (box.minimum.y + box.maximum.y) * .5f);
    frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    frame();
}
void exercise(const std::filesystem::path& root, float scale) {
    SceneDocument::create_project(root, "Link controls");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root);
    const auto id = authoring_command(scene, "entity.create", {{"name", "Connection"}, {"kind", 4}})
                        .at("selected")
                        .get<std::string>();
    authoring_command(scene, "component.add",
                      {{"entity", id}, {"component", "forge.navigation_link"}});
    ComponentInspector inspector;
    Ui ui(scale, {760 * scale, 1600 * scale});
    const auto frame = [&] { ui.frame([&] { inspector.draw(scene, project, id); }); };
    frame();
    frame();
    for (const auto* field : {"enabled", "bidirectional", "plane", "profile", "start_x", "start_y",
                              "start_z", "end_x", "end_y", "end_z", "radius", "arc_height"})
        check(test::ui_targets.contains(std::string("component-field:forge.navigation_link:") +
                                        field),
              "Link Inspector omitted a reflected field");
    click(frame, "component-field:forge.navigation_link:bidirectional");
    check(!scene.entity(id).get<NavigationLink>().bidirectional,
          "Real link direction checkbox did not commit");
    check(scene.undo() && scene.entity(id).get<NavigationLink>().bidirectional && scene.redo() &&
              !scene.entity(id).get<NavigationLink>().bidirectional,
          "Link widget did not use ordinary Scene Undo/Redo");
    click(frame, "component-field:forge.navigation_link:plane");
    click(frame, "property:choice:plane:1");
    check(scene.entity(id).get<NavigationLink>().plane == 1,
          "Real typed Plane dropdown did not commit XY selection");
    check(scene.undo() && scene.entity(id).get<NavigationLink>().plane == 0 && scene.redo(),
          "Typed Plane widget did not preserve native numeric source/history");
    auto& input = ImGui::GetIO();
    input.AddKeyEvent(ImGuiMod_Ctrl, true);
    click(frame, "component-field:forge.navigation_link:arc_height");
    input.AddKeyEvent(ImGuiKey_A, true);
    frame();
    input.AddKeyEvent(ImGuiKey_A, false);
    input.AddKeyEvent(ImGuiMod_Ctrl, false);
    input.AddInputCharactersUTF8("1.5");
    frame();
    input.AddKeyEvent(ImGuiKey_Enter, true);
    frame();
    input.AddKeyEvent(ImGuiKey_Enter, false);
    frame();
    check(scene.entity(id).get<NavigationLink>().arc_height == 1.5f && scene.undo() &&
              scene.entity(id).get<NavigationLink>().arc_height == 0 && scene.redo() &&
              scene.entity(id).get<NavigationLink>().arc_height == 1.5f,
          "Actual arc-height control lost optional-default editing or Scene history");
    auto link = scene.entity(id).get<NavigationLink>();
    link.profile = "Bridge";
    scene.entity(id).set<NavigationLink>(link);
    project.save();
    const auto saved = scene.document();
    NavigationTools navigation(engine.world(), {});
    const auto catalog = AssetCatalog::open_project(root);
    navigation.select_profile(scene, project, catalog, "Bridge");
    navigation.show_overlay(true);
    const auto shared_catalog = std::make_shared<const AssetCatalog>(catalog);
    std::string message;
    const auto controls = [&] {
        ui.frame([&] {
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);
            navigation.content(scene, project, false, message, shared_catalog);
        });
    };
    controls();
    controls();
    check(!test::ui_targets.at("physics-layer-picker:##navigation_collision_layers").enabled,
          "Collision layer filter is enabled before opting into collection");
    click(controls, "navigation:setting:Collect static colliders");
    check(test::ui_targets.at("physics-layer-picker:##navigation_collision_layers").enabled,
          "Real collision collection checkbox did not enable its shared layer picker");
    check(scene.document() == saved, "Navigation build settings changed authored Scene history");
    click(controls, "navigation:setting:Collect static colliders");
    check(!test::ui_targets.at("physics-layer-picker:##navigation_collision_layers").enabled,
          "Disabling collision collection did not disable its layer filter");
    click(controls, "navigation:setting:Plane");
    click(controls, "navigation:plane:1");
    EditorCamera camera;
    camera.align(2, 1);
    camera.distance = 8;
    const auto overlay = [&] {
        ui.frame([&] {
            navigation.draw(scene.effective_document(), camera, {30, 30},
                            {600 * scale, 500 * scale});
        });
    };
    overlay();
    overlay();
    const auto guide = "navigation:link:" + id;
    check(test::ui_targets.contains(guide),
          "Selected profile has no transformed link direction guide");
    const auto curved = test::ui_targets.at(guide);
    authoring_command(scene, "property.set",
                      {{"entity", id},
                       {"component", "forge.navigation_link"},
                       {"field", "arc_height"},
                       {"value", 0}});
    overlay();
    const auto straight = test::ui_targets.at(guide);
    check(curved.minimum.y < straight.minimum.y - 20 * scale && scene.undo(),
          "Navigation overlay did not show authored world-Y lift or preserve source history");
    overlay();
    navigation.select_profile(scene, project, catalog, "Other");
    overlay();
    check(!test::ui_targets.contains(guide), "Another profile displayed an unselected link guide");
    navigation.select_profile(scene, project, catalog, "Bridge");
    navigation.show_overlay(false);
    overlay();
    check(!test::ui_targets.contains(guide) && scene.document() == saved,
          "Overlay visibility/profile selection changed the authored Scene");
    Json progress = {{"phase", "Paused"},
                     {"sampled", false},
                     {"tick", 12},
                     {"link", {{"source", id}, {"entry", {-1, 0, 0}}, {"exit", {1, 0, 0}}}}};
    ui.frame([&] { inspector.draw_navigation_state(progress); });
    check(test::ui_targets.contains("navigation_agent:link"),
          "Held traversal progress is absent from the Inspector");
    progress["link"]["arc_height"] = 1.5;
    ui.frame([&] { inspector.draw_navigation_state(progress); });
    check(test::ui_targets.contains("navigation_agent:arc_height"),
          "Held arc progress omitted its admitted height");
}
void status(const std::filesystem::path& root, const std::filesystem::path& worker) {
    SceneDocument::create_project(root, "Link build profiles");
    EngineContext engine;
    Scene scene(engine.world());
    std::vector<std::string> recent;
    EditorFiles files(scene, nullptr, recent);
    files.start(root);
    for (float x : {-3.f, 3.f}) {
        const auto id = authoring_command(scene, "entity.create", {{"name", "Island"}, {"kind", 4}})
                            .at("selected")
                            .get<std::string>();
        scene.entity(id).set<LocalTranslation>({x, 0, 0}).set<NavigationRegion2D>(
            {true, false, 4, 6});
    }
    const auto id = authoring_command(scene, "entity.create", {{"name", "Bridge"}, {"kind", 4}})
                        .at("selected")
                        .get<std::string>();
    NavigationLink link;
    link.plane = 1;
    link.profile = "Bridge";
    link.start_x = -2;
    link.end_x = 2;
    scene.entity(id).set<NavigationLink>(link);
    files.document.save();
    NavigationTools navigation(engine.world(), worker);
    ui::AutomationPermissions permissions;
    permissions.assets = true;
    const auto methods = NavigationAutomation::methods(navigation, scene, files, permissions, {});
    const auto build = std::find_if(methods.begin(), methods.end(), [](const auto& method) {
        return method.name == "navigation.build";
    });
    check(build != methods.end(), "Navigation build owner is absent from automation discovery");
    for (const auto* name : {"Other", "Bridge"}) {
        NavigationSettings settings;
        settings.plane = NavigationPlane::PlanarXY;
        settings.profile = name;
        build->invoke({{"settings", settings}});
        std::string message;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (navigation.pending()) {
            navigation.poll(scene, files.document, false, message);
            check(std::chrono::steady_clock::now() < deadline, "Link editor build timed out");
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        check(NavigationAutomation::state(navigation, scene, files).at("error") == "",
              "Existing automation build owner rejected linked profile publication");
    }
    Ui ui(1, {1000, 1100});
    std::string message;
    const auto frame = [&] {
        ui.frame([&] {
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);
            navigation.content(
                scene, files.document, false, message,
                std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root)));
        });
    };
    frame();
    frame();
    check(test::ui_targets.contains("navigation:status:NavMesh ready") &&
              NavigationAutomation::state(navigation, scene, files).at("link_count") == 1,
          "Editor status or automation lost the linked profile");
    link.end_x += .1f;
    scene.entity(id).set<NavigationLink>(link);
    frame();
    check(test::ui_targets.contains("navigation:status:Navigation is stale — rebuild before Play"),
          "Editor status did not detect its named profile's link edit");
    navigation.select_profile(scene, files.document, AssetCatalog::open_project(root), "Other");
    frame();
    check(test::ui_targets.contains("navigation:status:NavMesh ready") &&
              NavigationAutomation::state(navigation, scene, files).at("link_count") == 0,
          "An unrelated profile became stale or exposed another profile's links");
}
void capture_gallery(const std::filesystem::path& gallery, const std::filesystem::path& output,
                     bool arcs = false) {
    const auto root = output / "Gallery";
    std::filesystem::create_directories(root);
    std::filesystem::copy(gallery, root, std::filesystem::copy_options::recursive);
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root);
    project.open_scene(root / (arcs ? "Scenes/218-navigation-link-arcs.scene.json"
                                    : "Scenes/140-navigation-links.scene.json"));
    std::string selected;
    const auto document = scene.document();
    for (const auto& entity : document.at("entities"))
        if (entity.at("name") == "Two-way connection - Loop profile")
            selected = entity.at("id");
    check(!selected.empty(), "Gallery link capture source is missing");
    test::ImGuiSoftwareCapture capture(860, 720);
    ui::style(1);
    test::observe_ui = true;
    ComponentInspector inspector;
    for (unsigned frame = 0; frame != 3; ++frame) {
        test::ui_targets.clear();
        capture.new_frame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({860, 720}, ImGuiCond_Always);
        ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        inspector.draw(scene, project, selected);
        ImGui::End();
        capture.render();
    }
    for (const auto* field : {"enabled", "bidirectional", "plane", "profile", "start_x", "end_x",
                              "radius", "arc_height"}) {
        const auto& box =
            test::ui_targets.at(std::string("component-field:forge.navigation_link:") + field);
        check(box.minimum.y >= box.clip_minimum.y && box.maximum.y <= box.clip_maximum.y &&
                  box.minimum.x >= box.clip_minimum.x && box.maximum.x <= box.clip_maximum.x,
              "Gallery capture hides an essential link control");
    }
    check(scene.document() == document && !project.dirty(),
          "Inspector capture changed the saved Gallery");
    capture.save(output /
                 (arcs ? "navigation-link-arc-inspector.png" : "navigation-link-inspector.png"));
    test::observe_ui = false;
}
void capture_navigation(const std::filesystem::path& gallery, const std::filesystem::path& output,
                        std::optional<NavigationPlane> area_plane = {}) {
    const bool classified = area_plane.has_value();
    const auto root = (classified ? output / AssetId::generate().str() : output) / "Gallery";
    std::filesystem::create_directories(root);
    std::filesystem::copy(gallery, root, std::filesystem::copy_options::recursive);
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root);
    const bool planar = area_plane == NavigationPlane::PlanarXY;
    project.open_scene(root / (classified
                                   ? (planar ? "Scenes/232-navigation-area-costs-xy.scene.json"
                                             : "Scenes/231-navigation-area-costs.scene.json")
                                   : "Scenes/141-navigation-collision-collection.scene.json"));
    const auto document = scene.document();
    const auto catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
    NavigationTools navigation(engine.world(), {});
    navigation.select_profile(scene, project, *catalog, classified ? "default" : "Physical");
    navigation.show_overlay(classified);
    test::ImGuiSoftwareCapture capture(classified ? 1200 : 860, classified ? 840 : 600);
    ui::style(1);
    test::observe_ui = true;
    std::string message;
    for (unsigned frame = 0; frame != 3; ++frame) {
        test::ui_targets.clear();
        capture.new_frame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize(classified ? ImVec2{430, 840} : ImVec2{860, 600},
                                 ImGuiCond_Always);
        ImGui::Begin("Content - Navigation", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        navigation.content(scene, project, false, message, catalog);
        ImGui::End();
        if (classified) {
            ImGui::SetNextWindowPos({430, 0}, ImGuiCond_Always);
            ImGui::SetNextWindowSize({770, 840}, ImGuiCond_Always);
            ImGui::Begin("Scene navigation overlay", nullptr,
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
            EditorCamera camera;
            camera.target = {0, 0, 0};
            camera.align(planar ? 2 : 1, 1);
            camera.projection = CameraProjection::Orthographic;
            camera.orthographic_height = 22;
            auto* draw = ImGui::GetWindowDrawList();
            const auto before = draw->VtxBuffer.Size;
            navigation.draw(scene.effective_document(), camera, {448, 38}, {734, 784});
            std::set<ImU32> colors;
            for (int i = before; i < draw->VtxBuffer.Size; ++i)
                colors.insert(draw->VtxBuffer[i].col);
            check(colors.size() >= 4, "Area overlay omitted distinct surface fills and outlines");
            navigation.show_overlay(false);
            const auto visible = draw->VtxBuffer.Size;
            navigation.draw(scene.effective_document(), camera, {448, 38}, {734, 784});
            check(draw->VtxBuffer.Size == visible, "Hidden area overlay still emitted triangles");
            navigation.show_overlay(true);
            ImGui::End();
        }
        capture.render();
    }
    const std::vector<std::string> keys =
        classified
            ? std::vector<std::string>{"navigation:profile-selected:default", "navigation:area:0",
                                       "navigation:area:2", "navigation:status:NavMesh ready"}
            : std::vector<std::string>{"navigation:profile-selected:Physical",
                                       "navigation:setting:Collect static colliders",
                                       "physics-layer-picker:##navigation_collision_layers",
                                       "navigation:status:NavMesh ready"};
    for (const auto& key : keys) {
        const auto& box = test::ui_targets.at(key);
        check(box.enabled && box.minimum.y >= box.clip_minimum.y &&
                  box.maximum.y <= box.clip_maximum.y && box.minimum.x >= box.clip_minimum.x &&
                  box.maximum.x <= box.clip_maximum.x,
              "Gallery capture hides or disables a collection control");
    }
    check(message.empty() && scene.document() == document && !project.dirty(),
          "Collection control capture changed the saved Gallery or reported an error");
    capture.save(output / (classified ? (planar ? "navigation-areas-overlay-xy.png"
                                                : "navigation-areas-overlay.png")
                                      : "navigation-collection-controls.png"));
    test::observe_ui = false;
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 4 && (std::string_view(argv[1]) == "--capture" ||
                          std::string_view(argv[1]) == "--capture-arcs")) {
            capture_gallery(std::filesystem::absolute(argv[2]), std::filesystem::absolute(argv[3]),
                            std::string_view(argv[1]) == "--capture-arcs");
            return 0;
        }
        if (argc == 4 && std::string_view(argv[1]) == "--capture-collection") {
            capture_navigation(std::filesystem::absolute(argv[2]),
                               std::filesystem::absolute(argv[3]));
            return 0;
        }
        if (argc == 4 && (std::string_view(argv[1]) == "--capture-areas" ||
                          std::string_view(argv[1]) == "--capture-areas-xy")) {
            capture_navigation(
                std::filesystem::absolute(argv[2]), std::filesystem::absolute(argv[3]),
                std::string_view(argv[1]) == "--capture-areas-xy" ? NavigationPlane::PlanarXY
                                                                  : NavigationPlane::Spatial3D);
            return 0;
        }
        check(argc == 3, "Need scratch root and matching navigation worker");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        for (float scale : {1.f, 1.5f, 2.f})
            exercise(root / std::to_string(scale), scale);
        status(root / "Build", std::filesystem::absolute(argv[2]));
        std::filesystem::remove_all(root);
        std::cout << "Navigation link Inspector/history, typed plane, profile overlay and "
                     "build/status owner PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
