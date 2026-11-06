#include "editor/component_inspector.hpp"
#include "imgui_software_capture.hpp"
#include <algorithm>
#include <forge/ui_components.hpp>
#include <iostream>

using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void exercise(const std::filesystem::path& scratch, float scale,
              const std::filesystem::path& image = {}) {
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    const auto project_root = scratch / AssetId::generate().str();
    std::filesystem::create_directories(project_root);
    project.open_project(project_root, true);
    const auto actor =
        authoring_command(scene, "entity.create", {{"name", "Interactive mesh panel"}})
            .at("selected")
            .get<std::string>();
    authoring_command(scene, "component.add",
                      {{"entity", actor}, {"component", "forge.ui_surface_input"}});
    const auto original = scene.document();
    const int height = image.empty() ? 900 : 320;
    test::ImGuiSoftwareCapture capture(1080, height);
    auto& io = ImGui::GetIO();
    io.ConfigInputTrickleEventQueue = false;
    ui::style(scale);
    ui::EditorUiContext context;
    ui::ContextScope scope(context);
    test::observe_ui = true;
    ComponentInspector inspector;
    inspector.fixture_focus_component = "forge.ui_surface_input";
    const auto frame = [&] {
        test::ui_targets.clear();
        capture.new_frame();
        ImGui::SetNextWindowPos({10, 10}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({1060, float(height - 20)}, ImGuiCond_Always);
        ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        inspector.draw(scene, project, actor);
        ImGui::End();
        capture.render();
    };
    const auto click = [&](const char* name) {
        frame();
        const auto found = test::ui_targets.find(name);
        check(found != test::ui_targets.end(), "Surface Inspector control is missing");
        const auto& box = found->second;
        const float left = std::max(box.minimum.x, box.clip_minimum.x),
                    right = std::min(box.maximum.x, box.clip_maximum.x),
                    top = std::max(box.minimum.y, box.clip_minimum.y),
                    bottom = std::min(box.maximum.y, box.clip_maximum.y);
        check(box.enabled && right > left && bottom > top,
              "Surface Inspector control is disabled or clipped");
        io.AddMousePosEvent((left + right) * .5f, (top + bottom) * .5f);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
    };
    for (unsigned i = 0; i < 3; ++i)
        frame();
    check(test::ui_targets.contains("component-field:forge.ui_surface_input:render_texture") &&
              test::ui_targets.contains("component-field:forge.ui_surface_input:enabled"),
          "Surface Inspector lost its output or enable control");
    click("component-field:forge.ui_surface_input:geometry");
    click("property:choice:geometry:1");
    check(scene.entity(actor).get<UiSurfaceInput>().geometry == 1,
          "Geometry dropdown did not select Mesh through shared authoring");
    check(scene.undo() && scene.document() == original && scene.redo() &&
              scene.entity(actor).get<UiSurfaceInput>().geometry == 1,
          "Inspector geometry change did not preserve ordinary Undo and Redo");
    click("component-field:forge.ui_surface_input:geometry");
    click("property:choice:geometry:0");
    check(scene.entity(actor).get<UiSurfaceInput>().geometry == 0,
          "Geometry dropdown did not restore Quad mode");
    check(scene.undo(), "Surface Inspector final Undo failed");
    for (unsigned i = 0; i < 3; ++i)
        frame();
    if (!image.empty())
        capture.save(image);
    test::observe_ui = false;
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2 || argc == 4, "Need scratch, optionally --capture image.png");
        const auto scratch = std::filesystem::absolute(argv[1]);
        if (argc == 4) {
            check(std::string_view(argv[2]) == "--capture", "Unknown surface capture option");
            exercise(scratch, 1, std::filesystem::absolute(argv[3]));
        } else {
            exercise(scratch, 1);
            exercise(scratch, 1.5f);
        }
        std::cout << "Native surface Inspector geometry choices, authoring and history passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
