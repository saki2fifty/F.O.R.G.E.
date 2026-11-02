#include "component_inspector.hpp"
#include "imgui_software_capture.hpp"
#include "navigation_region_gesture.hpp"
#include "spatial_helpers.hpp"
#include <iostream>
using namespace forge;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
void exercise(const std::filesystem::path& root, float scale, ImVec2 size,
              const std::filesystem::path& images) {
    SceneDocument::create_project(root, "Polygon navigation");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root);
    const auto actor =
        authoring_command(
            scene, "entity.create",
            {{"kind", 4}, {"name", "Region"}, {"position", {{"x", 0}, {"y", 0}, {"z", 0}}}})
            .at("selected")
            .get<std::string>();
    authoring_command(scene, "component.add",
                      {{"entity", actor}, {"component", "forge.navigation_region_2d"}});
    const auto rectangle = scene.document();
    test::ImGuiSoftwareCapture capture(unsigned(size.x), unsigned(size.y));
    ui::style(scale);
    ui::EditorUiContext context;
    ui::ContextScope scope(context);
    ComponentInspector inspector;
    inspector.fixture_focus_component = "forge.navigation_region_2d";
    test::observe_ui = true;
    auto frame = [&] {
        test::ui_targets.clear();
        capture.new_frame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        inspector.draw(scene, project, actor);
        ImGui::End();
        capture.render();
    };
    const auto visible = [&](const std::string& key) {
        for (unsigned attempt = 0; attempt < 30; ++attempt) {
            frame();
            const auto box = test::ui_targets.at(key);
            check(box.minimum.x >= box.clip_minimum.x && box.maximum.x <= box.clip_maximum.x,
                  "Polygon conversion control clips horizontally");
            if (box.minimum.y >= box.clip_minimum.y && box.maximum.y <= box.clip_maximum.y)
                return box;
            auto& io = ImGui::GetIO();
            io.AddMousePosEvent((box.clip_minimum.x + box.clip_maximum.x) * .5f,
                                (box.clip_minimum.y + box.clip_maximum.y) * .5f);
            io.AddMouseWheelEvent(0, box.minimum.y < box.clip_minimum.y ? 2 : -2);
        }
        throw std::runtime_error("Polygon conversion control cannot be reached by scrolling");
    };
    const auto click = [&](const std::string& key) {
        const auto box = visible(key);
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent((box.minimum.x + box.maximum.x) * .5f,
                            (box.minimum.y + box.maximum.y) * .5f);
        frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        frame();
    };
    frame();
    frame();
    // Initial fixture focus sets a scroll target; normal input must own later scrolling.
    inspector.fixture_focus_component.clear();
    check(test::ui_targets.contains("navigation:region-create-polygon") &&
              test::ui_targets.contains("component-field:forge.navigation_region_2d:width"),
          "Rectangle lost its extents or valid polygon construction control");
    click("navigation:region-create-polygon");
    if (scene.entity(actor).get<NavigationRegion2D>().points.size() != 4) {
        capture.save(root / "conversion-failed.png");
        throw std::runtime_error(
            "Actual polygon conversion did not create four native points; scale=" +
            std::to_string(scale) + " width=" + std::to_string(size.x) +
            " capture=" + (root / "conversion-failed.png").string());
    }
    const auto converted = scene.document();
    check(scene.undo() && scene.document() == rectangle && scene.redo() &&
              scene.document() == converted,
          "Polygon conversion was not one complete Undo/Redo operation");
    frame();
    check(!test::ui_targets.contains("component-field:forge.navigation_region_2d:width") &&
              test::ui_targets.contains("navigation:region-use-rectangle"),
          "Polygon Inspector displayed unused rectangle extents");
    const Json points = Json::array({{{"x", -4}, {"y", -4}},
                                     {{"x", 4}, {"y", -4}},
                                     {{"x", 4}, {"y", -1}},
                                     {{"x", -1}, {"y", -1}},
                                     {{"x", -1}, {"y", 4}},
                                     {{"x", -4}, {"y", 4}}});
    authoring_command(scene, "property.set",
                      {{"entity", actor},
                       {"component", "forge.navigation_region_2d"},
                       {"field", "points"},
                       {"value", points}});
    const auto polygon = scene.document();
    if (!images.empty()) {
        click("component-field:forge.navigation_region_2d:points");
        frame();
        capture.save(images / ("polygon-inspector-" + std::to_string(unsigned(scale * 100)) + "-" +
                               std::to_string(unsigned(size.x)) + ".png"));
    }
    auto source = scene.effective_document();
    const auto view = navigation_region_view(source, actor);
    check(view && view->outline.size() == 6, "Polygon view did not retain its concave boundary");
    ui::SpatialHelpers helpers;
    helpers.update(source, 1, 1000, 800);
    check(helpers.markers().size() == 1 && helpers.markers()[0].lines.size() == 6 &&
              helpers.markers()[0].note.find("6 polygon points") != std::string::npos,
          "Polygon guide substituted rectangle edges");
    EditorCamera camera;
    camera.align(2, -1);
    camera.target = {0, 0, 0};
    camera.projection = CameraProjection::Orthographic;
    camera.orthographic_height = 20;
    camera.distance = 20;
    const auto edge = project_point(camera, {0, -4, 0}, 1000, 800);
    check(edge && helpers.pick(camera, {1000, 800}, {(*edge)[0], (*edge)[1]}, actor) == actor,
          "Polygon outline was not selectable through the existing helper owner");
    NavigationRegionGesture gesture;
    check(!gesture.begin(scene, actor, 0, 1) && scene.document() == polygon,
          "Rectangle resize silently edited polygon dimensions");
    click("navigation:region-use-rectangle");
    check(scene.entity(actor).get<NavigationRegion2D>().points.empty() && scene.undo() &&
              scene.document() == polygon && scene.redo() &&
              scene.entity(actor).get<NavigationRegion2D>().points.empty(),
          "Use rectangle did not preserve the polygon through history");
    test::observe_ui = false;
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2 || argc == 3, "Need polygon editor scratch root and optional captures");
        const auto images =
            argc == 3 ? std::filesystem::absolute(argv[2]) : std::filesystem::path{};
        if (!images.empty())
            std::filesystem::create_directories(images);
        const auto root = std::filesystem::absolute(argv[1]);
        std::filesystem::create_directories(root);
        for (float scale : {1.f, 1.5f, 2.f})
            for (const auto size : {ImVec2{420, 640}, ImVec2{1000, 900}})
                exercise(root / AssetId::generate().str(), scale, size, images);
        std::cout << "Actual polygon Inspector conversion/history, contour guides/picking and "
                     "rectangle gesture isolation PASS at three scales/two sizes\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
