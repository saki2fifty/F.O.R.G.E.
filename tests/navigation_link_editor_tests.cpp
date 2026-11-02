#include "editor/component_inspector.hpp"
#include "editor/navigation_automation.hpp"
#include "imgui_software_capture.hpp"
#include <imgui_internal.h>
#include <iostream>
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
                              "start_z", "end_x", "end_y", "end_z", "radius"})
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
void capture_gallery(const std::filesystem::path& gallery, const std::filesystem::path& output) {
    const auto root = output / "Gallery";
    std::filesystem::create_directories(root);
    std::filesystem::copy(gallery, root, std::filesystem::copy_options::recursive);
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root);
    project.open_scene(root / "Scenes/140-navigation-links.scene.json");
    std::string selected;
    const auto document = scene.document();
    for (const auto& entity : document.at("entities"))
        if (entity.at("name") == "Two-way connection - Loop profile")
            selected = entity.at("id");
    check(!selected.empty(), "Gallery link capture source is missing");
    test::ImGuiSoftwareCapture capture(860, 560);
    ui::style(1);
    test::observe_ui = true;
    ComponentInspector inspector;
    for (unsigned frame = 0; frame != 3; ++frame) {
        test::ui_targets.clear();
        capture.new_frame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({860, 560}, ImGuiCond_Always);
        ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        inspector.draw(scene, project, selected);
        ImGui::End();
        capture.render();
    }
    for (const auto* field :
         {"enabled", "bidirectional", "plane", "profile", "start_x", "end_x", "radius"}) {
        const auto& box =
            test::ui_targets.at(std::string("component-field:forge.navigation_link:") + field);
        check(box.minimum.y >= box.clip_minimum.y && box.maximum.y <= box.clip_maximum.y &&
                  box.minimum.x >= box.clip_minimum.x && box.maximum.x <= box.clip_maximum.x,
              "Gallery capture hides an essential link control");
    }
    check(scene.document() == document && !project.dirty(),
          "Inspector capture changed the saved Gallery");
    capture.save(output / "navigation-link-inspector.png");
    test::observe_ui = false;
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 4 && std::string_view(argv[1]) == "--capture") {
            capture_gallery(std::filesystem::absolute(argv[2]), std::filesystem::absolute(argv[3]));
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
