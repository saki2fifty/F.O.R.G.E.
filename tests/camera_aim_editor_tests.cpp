#include "editor/component_inspector.hpp"
#include "imgui_software_capture.hpp"
#include <forge/authoring.hpp>
#include <iostream>
#include <type_traits>

using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class Modifier>
void exercise(const std::filesystem::path& scratch, float scale,
              const std::filesystem::path& image = {}) {
    constexpr bool follows = std::is_same_v<Modifier, CameraFollow>;
    const std::string component = follows ? "forge.camera_follow" : "forge.camera_aim";
    const auto field_key = [&](const char* field) {
        return "component-field:" + component + ":" + field;
    };
    const auto root = scratch / AssetId::generate().str();
    std::filesystem::create_directories(scratch);
    SceneDocument::create_project(root, "Camera target controls");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root, true);
    const auto create = [&](const char* name, const char* recipe) {
        return authoring_command(scene, "entity.create", {{"name", name}, {"recipe", recipe}})
            .at("selected")
            .get<std::string>();
    };
    const auto target = create("Moving target", "empty");
    const auto actor = create("Tracking Camera", "render.camera");
    authoring_command(scene, "component.add", {{"entity", actor}, {"component", component}});
    authoring_command(scene, "property.set_many",
                      {{"entity", actor},
                       {"component", component},
                       {"values",
                        {{"target", EntityRef{scene.asset_id(), EntityId::parse(target)}},
                         {"offset_y", follows ? 2. : .5}}}});
    const auto before = scene.document();
    const int height = image.empty() ? 760 : follows ? 400 : 360;
    test::ImGuiSoftwareCapture capture(780, height);
    auto& io = ImGui::GetIO();
    io.ConfigInputTrickleEventQueue = false;
    ui::style(scale);
    ui::EditorUiContext context;
    context.scene = &scene;
    ui::ContextScope scope(context);
    test::observe_ui = true;
    ComponentInspector inspector;
    inspector.fixture_focus_component = component;
    const auto frame = [&] {
        test::ui_targets.clear();
        capture.new_frame();
        ImGui::SetNextWindowPos({8, 8}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({764, float(height - 16)}, ImGuiCond_Always);
        ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        inspector.draw(scene, project, actor);
        ImGui::End();
        capture.render();
    };
    for (unsigned index = 0; index < 4; ++index)
        frame();
    const auto click = [&](const std::string& name) {
        frame();
        const auto found = test::ui_targets.find(name);
        check(found != test::ui_targets.end(), "Camera Aim picker control is unavailable");
        const auto box = found->second;
        const auto left = std::max(box.minimum.x, box.clip_minimum.x);
        const auto right = std::min(box.maximum.x, box.clip_maximum.x);
        const auto top = std::max(box.minimum.y, box.clip_minimum.y);
        const auto bottom = std::min(box.maximum.y, box.clip_maximum.y);
        check(box.enabled && right > left && bottom > top, "Camera Aim picker is clipped");
        io.AddMousePosEvent((left + right) * .5f, (top + bottom) * .5f);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
    };
    click(field_key("target"));
    click("entity-picker-clear");
    check(!scene.entity(actor).template get<Modifier>().target.entity && scene.undo() &&
              scene.document() == before && scene.redo() &&
              !scene.entity(actor).template get<Modifier>().target.entity,
          "Camera Aim target clear lost typed Scene history");
    click(field_key("target"));
    click("entity-picker-row:" + target);
    check(scene.document() == before && scene.undo() &&
              !scene.entity(actor).template get<Modifier>().target.entity && scene.redo() &&
              scene.document() == before,
          "Camera Aim target selection lost typed Scene history");
    if constexpr (follows) {
        click(field_key("offset_space"));
        click("property:choice:offset_space:1");
        check(scene.entity(actor).template get<Modifier>().offset_space == 1 && scene.undo() &&
                  scene.document() == before && scene.redo() &&
                  scene.entity(actor).template get<Modifier>().offset_space == 1 && scene.undo() &&
                  scene.document() == before,
              "Camera Follow coordinate-space choice lost typed Scene history");
    }
    frame();
    for (const auto* field : {"enabled", "target", "offset_x", "offset_y", "offset_z", "weight"}) {
        const auto found = test::ui_targets.find(field_key(field));
        check(found != test::ui_targets.end() && found->second.enabled,
              "Native Camera Aim Inspector lost an editable reflected field");
    }
    const auto box = test::ui_targets.at(field_key("weight"));
    const auto x = std::max(box.minimum.x, box.clip_minimum.x);
    const auto right = std::min(box.maximum.x, box.clip_maximum.x);
    const auto y = std::max(box.minimum.y, box.clip_minimum.y);
    const auto bottom = std::min(box.maximum.y, box.clip_maximum.y);
    check(right > x && bottom > y, "Camera Aim weight is clipped");
    io.AddMousePosEvent(std::lerp(x, right, .65f), (y + bottom) * .5f);
    frame();
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddKeyEvent(ImGuiKey_A, true);
    frame();
    io.AddKeyEvent(ImGuiKey_A, false);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    frame();
    io.AddInputCharactersUTF8("0.4");
    frame();
    io.AddKeyEvent(ImGuiKey_Enter, true);
    frame();
    io.AddKeyEvent(ImGuiKey_Enter, false);
    frame();
    check(std::abs(scene.entity(actor).template get<Modifier>().weight - .4) < 1e-6 &&
              scene.undo() && scene.document() == before && scene.redo() &&
              std::abs(scene.entity(actor).template get<Modifier>().weight - .4) < 1e-6,
          "Native Camera Aim weight edit lost its ordinary Scene history");
    check(scene.undo() && scene.document() == before, "Camera Aim Inspector restore failed");
    for (unsigned index = 0; index < 4; ++index)
        frame();
    if (!image.empty())
        capture.save(image);
    test::observe_ui = false;
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc >= 2, "Need scratch and optional --follow and --capture image.png");
        const auto scratch = std::filesystem::absolute(argv[1]);
        int argument = 2;
        const bool follows = argument < argc && std::string_view(argv[argument]) == "--follow";
        if (follows)
            ++argument;
        std::filesystem::path image;
        if (argument < argc) {
            check(argument + 2 == argc && std::string_view(argv[argument]) == "--capture",
                  "Unknown Camera target controls option");
            image = std::filesystem::absolute(argv[argument + 1]);
        }
        const auto run = [&](float scale) {
            if (follows)
                exercise<CameraFollow>(scratch, scale, image);
            else
                exercise<CameraAim>(scratch, scale, image);
        };
        if (!image.empty())
            run(1);
        else
            for (const auto scale : {1.f, 1.5f, 2.f})
                run(scale);
        std::cout
            << "Native Camera target fields, choices, weight interaction and Undo/Redo passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
