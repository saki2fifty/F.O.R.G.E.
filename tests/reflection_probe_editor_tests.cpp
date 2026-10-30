#include "editor/camera.hpp"
#include "editor/component_inspector.hpp"
#include "editor/creation_menu.hpp"
#include "editor/spatial_helpers.hpp"
#include "imgui_software_capture.hpp"
#include <forge/engine_assets.hpp>
#include <imgui_internal.h>
#include <iostream>
using namespace forge;
namespace {
void check(bool condition, const std::string& message) {
    if (!condition)
        throw std::runtime_error(message);
}
void exercise(const std::filesystem::path& root, float scale, ImVec2 display) {
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    const auto project_root = root / AssetId::generate().str();
    std::filesystem::create_directories(project_root);
    project.open_project(project_root, true);
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = display;
    io.DeltaTime = 1.f / 60;
    io.ConfigInputTrickleEventQueue = false;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ui::style(scale);
    ui::EditorUiContext context;
    ui::ContextScope scope(context);
    test::observe_ui = true;
    ComponentInspector inspector;
    ui::EditorActions actions;
    std::string selected;
    for (const auto& recipe : entity_recipes())
        actions.entries.push_back({"Create / " + recipe.label, recipe.label, "", "Create entity",
                                   true, [&, id = recipe.id] {
                                       selected = authoring_command(scene, "entity.create",
                                                                    {{"recipe", id}})
                                                      .at("selected")
                                                      .get<std::string>();
                                   }});
    bool at_target = false;
    const auto frame = [&] {
        test::ui_targets.clear();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({5, 5}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({display.x - 10, display.y - 10}, ImGuiCond_Always);
        ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_MenuBar);
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("Entity")) {
                ui::creation_menu(actions, at_target);
                ImGui::EndMenu();
            }
            FORGE_UI_PROBE("probe-test:entity-menu");
            ImGui::EndMenuBar();
        }
        inspector.draw(scene, project, selected);
        ImGui::End();
        ImGui::Render();
    };
    const auto click = [&](const std::string& key) {
        frame();
        check(test::ui_targets.contains(key), "Probe UI target missing: " + key);
        auto box = test::ui_targets.at(key);
        for (unsigned attempt = 0; attempt < 3 && (std::min(box.maximum.y, box.clip_maximum.y) -
                                                   std::max(box.minimum.y, box.clip_minimum.y)) <
                                                      ImGui::GetFontSize() * scale;
             ++attempt) {
            auto* window = ImGui::FindWindowByName("Inspector");
            ImGui::SetScrollY(window, window->Scroll.y + (box.minimum.y + box.maximum.y -
                                                          box.clip_minimum.y - box.clip_maximum.y) *
                                                             .5f);
            frame();
            frame();
            box = test::ui_targets.at(key);
        }
        const float left = std::max(box.minimum.x, box.clip_minimum.x),
                    right = std::min(box.maximum.x, box.clip_maximum.x),
                    top = std::max(box.minimum.y, box.clip_minimum.y),
                    bottom = std::min(box.maximum.y, box.clip_maximum.y);
        check(box.enabled && right > left && bottom - top >= ImGui::GetFontSize() * scale,
              "Probe UI target is disabled or clipped: " + key);
        io.AddMousePosEvent((left + right) * .5f, (top + bottom) * .5f);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
        frame();
    };
    const auto type = [&](const std::string& field, const char* text) {
        click(field.starts_with("asset-search:") || field.starts_with("component-field:")
                  ? field
                  : "component-field:forge.reflection_probe:" + field);
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        io.AddKeyEvent(ImGuiKey_A, true);
        frame();
        io.AddKeyEvent(ImGuiKey_A, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        io.AddInputCharactersUTF8(text);
        frame();
        io.AddKeyEvent(ImGuiKey_Enter, true);
        frame();
        io.AddKeyEvent(ImGuiKey_Enter, false);
        frame();
    };
    frame();
    const auto empty = scene.document();
    click("probe-test:entity-menu");
    click("category:Rendering");
    click("action:Create / Reflection Probe");
    check(!selected.empty() && scene.entity(selected).has<ReflectionProbe>() &&
              !scene.entity(selected).has<MeshRenderer>(),
          "Probe menu did not create its component composition");
    const auto created = scene.document();
    check(scene.undo() && scene.document() == empty && scene.redo() && scene.document() == created,
          "Probe creation was not one atomic Undo/Redo step");
    const auto white = engine_texture(EngineTexture::White, TextureDimension::Cube);
    click("asset-picker:texture:##Reflection map");
    type("asset-search:texture:##Reflection map", "White / Cube");
    click("picker-option:" + white.id.str());
    check(scene.entity(selected).get<ReflectionProbe>().texture == white,
          "Probe Inspector texture picker bypassed the native reference field");
    type("half_extent_x", "0.0001");
    type("intensity", "2.5");
    type("priority", "-7");
    type("rotation_degrees", "90");
    auto value = scene.entity(selected).get<ReflectionProbe>();
    check(value.half_extent_x == .0001 && value.intensity == 2.5 && value.priority == -7 &&
              value.rotation_degrees == 90,
          "Probe numeric Inspector controls lost bounds, precision or values");
    const auto before_toggle = scene.document();
    click("component-field:forge.reflection_probe:box_projection");
    check(!scene.entity(selected).get<ReflectionProbe>().box_projection && scene.undo() &&
              scene.document() == before_toggle,
          "Probe Boolean control bypassed native history");
    authoring_command(scene, "property.set",
                      {{"entity", selected},
                       {"component", "forge.reflection_probe"},
                       {"field", "half_extent_x"},
                       {"value", 5.}});
    const auto schema = scene.schema();
    for (const auto& type : schema.at("components")) {
        if (type.at("id") != "forge.reflection_probe")
            continue;
        const auto components = scene.document().at("entities").at(0).at("components");
        check(ComponentInspector::runtime_type_available(type, components),
              "Probe controls unavailable in Live gameplay");
        for (const auto& field : type.at("fields"))
            check(ComponentInspector::runtime_field_available(
                      "forge.reflection_probe", field, components) == (field.at("id") != "texture"),
                  "Probe Live gameplay differs from the shared runtime admission");
    }
    const auto saved = scene.document();
    const auto markers = ui::spatial_markers(scene.effective_document(), 800, 600, 10);
    check(markers.size() == 1 && markers[0].probe && markers[0].enabled &&
              markers[0].lines.size() == 24 && markers[0].note.find("world m") != std::string::npos,
          "Probe influence and world-distance fade guides are missing");
    EditorCamera camera;
    check(camera.frame(scene.effective_document(), selected, 1.5f) && camera.distance > 8,
          "Probe framing ignored the full influence volume");
    check(scene.document() == saved, "Probe guide generation changed authored state");
    scene.save(project.project() / "probe.scene.json");
    Scene reopened(engine.world());
    reopened.load(project.project() / "probe.scene.json");
    check(reopened.document() == saved, "Probe Inspector values did not save and reopen");
    click("probe-test:entity-menu");
    click("category:Rendering");
    click("action:Create / Light");
    check(scene.entity(selected).has<Light>(), "Light creation menu lost its component");
    const auto before_sources = scene.document();
    type("component-field:forge.light:shadow_source_radius", "0.25");
    type("component-field:forge.light:shadow_angular_radius", "0.01");
    check(scene.entity(selected).get<Light>().shadow_source_radius == .25f &&
              scene.entity(selected).get<Light>().shadow_angular_radius == .01f,
          "Light Inspector did not edit both shadow source dimensions");
    const auto sources = scene.document();
    check(scene.undo() && scene.undo() && scene.document() == before_sources && scene.redo() &&
              scene.redo() && scene.document() == sources,
          "Light source Inspector fields bypassed ordinary history");
    type("component-field:forge.light:shadow_source_radius", "-1");
    check(scene.document() == sources, "Invalid source radius changed the Scene");
    type("component-field:forge.light:shadow_source_radius", "0.25");
    scene.save(project.project() / "light.scene.json");
    reopened.load(project.project() / "light.scene.json");
    check(reopened.document() == sources, "Light source Inspector values did not save/reopen");
    Json light_components;
    for (const auto& row : sources.at("entities"))
        if (row.at("id") == selected)
            light_components = row.at("components");
    for (const auto& type : schema.at("components")) {
        if (type.at("id") != "forge.light")
            continue;
        check(ComponentInspector::runtime_type_available(type, light_components),
              "Light source controls missing from Live gameplay");
        for (const auto& field : type.at("fields"))
            check(ComponentInspector::runtime_field_available("forge.light", field,
                                                              light_components) ==
                      (field.at("id") == "shadow_source_radius" ||
                       field.at("id") == "shadow_angular_radius"),
                  "Punctual Live controls exposed inactive area dimensions");
    }
    click("component-field:forge.light:cast_shadows");
    click("component-field:forge.light:kind");
    click("property:choice:kind:3");
    check(scene.entity(selected).get<Light>().kind == unsigned(LightKind::Rectangle),
          "Light kind picker did not select rectangular emission");
    frame();
    check(scene.entity(selected).get<Light>().cast_shadows &&
              test::ui_targets.contains("component-field:forge.light:cast_shadows"),
          "Carried shadow request cannot be cleared after selecting area emission");
    click("component-field:forge.light:cast_shadows");
    frame();
    check(!test::ui_targets.contains("component-field:forge.light:emitter_radius") &&
              !test::ui_targets.contains("component-field:forge.light:shadow_source_radius"),
          "Rectangle Inspector exposed inactive shape controls");
    const auto before_rectangle = scene.document();
    type("component-field:forge.light:emitter_width", "2");
    type("component-field:forge.light:emitter_height", "0.5");
    check(scene.entity(selected).get<Light>().emitter_width == 2 &&
              scene.entity(selected).get<Light>().emitter_height == .5f,
          "Rectangle Inspector dimensions did not reach native Light values");
    const auto rectangle = scene.document();
    check(scene.undo() && scene.undo() && scene.document() == before_rectangle && scene.redo() &&
              scene.redo() && scene.document() == rectangle,
          "Area Inspector dimensions bypassed ordinary history");
    type("component-field:forge.light:emitter_width", "0");
    check(scene.document() == rectangle, "Invalid emitter dimension changed authored state");
    type("component-field:forge.light:emitter_width", "2");
    click("component-field:forge.light:kind");
    click("property:choice:kind:4");
    type("component-field:forge.light:emitter_radius", "0.75");
    check(scene.entity(selected).get<Light>().emitter_radius == .75f &&
              scene.entity(selected).get<Light>().kind == unsigned(LightKind::Sphere),
          "Sphere picker and Inspector radius did not reach native Light values");
    frame();
    check(!test::ui_targets.contains("component-field:forge.light:emitter_width") &&
              !test::ui_targets.contains("component-field:forge.light:basis"),
          "Sphere Inspector exposed inactive orientation or rectangular dimensions");
    for (unsigned kind : {3u, 4u}) {
        light_components["forge.light"]["kind"] = kind;
        for (const auto& type : schema.at("components"))
            if (type.at("id") == "forge.light")
                for (const auto& field : type.at("fields")) {
                    const bool expected = kind == 3 ? field.at("id") == "emitter_width" ||
                                                          field.at("id") == "emitter_height"
                                                    : field.at("id") == "emitter_radius";
                    check(ComponentInspector::runtime_field_available("forge.light", field,
                                                                      light_components) == expected,
                          "Area Live Inspector exposed the wrong shape's fields");
                }
    }
    scene.save(project.project() / "area-light.scene.json");
    reopened.load(project.project() / "area-light.scene.json");
    check(reopened.document() == scene.document(), "Area Inspector did not save and reopen");
    test::observe_ui = false;
    ImGui::DestroyContext();
}
void capture(const std::filesystem::path& root, bool light = false,
             LightKind light_kind = LightKind::Directional) {
    std::filesystem::create_directories(root / "project");
    test::ImGuiSoftwareCapture capture(
        light ? 1080 : 760,
        light ? (light_kind == LightKind::Rectangle || light_kind == LightKind::Sphere ? 560 : 900)
              : 520);
    ui::style(1);
    ui::EditorUiContext context;
    ui::ContextScope scope(context);
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root / "project", true);
    const std::string id =
        authoring_command(scene, "entity.create",
                          {{"recipe", light ? "render.light" : "render.reflection_probe"},
                           {"name", !light                               ? "Room reflections"
                                    : light_kind == LightKind::Rectangle ? "Rectangular area light"
                                    : light_kind == LightKind::Sphere ? "Spherical area light"
                                                                      : "Soft directional light"}})
            .at("selected");
    if (light) {
        authoring_command(
            scene, "property.set_many",
            {{"entity", id},
             {"component", "forge.light"},
             {"values", light_kind == LightKind::Directional
                            ? Json{{"cast_shadows", true}, {"shadow_angular_radius", .01}}
                            : Json{{"kind", unsigned(light_kind)},
                                   {"intensity", 8},
                                   {"range", 8},
                                   {"emitter_radius", .75},
                                   {"emitter_width", 2},
                                   {"emitter_height", .5}}}});
    } else
        authoring_command(
            scene, "property.set",
            {{"entity", id},
             {"component", "forge.reflection_probe"},
             {"field", "texture"},
             {"value", engine_texture(EngineTexture::White, TextureDimension::Cube).id}});
    ComponentInspector inspector;
    inspector.fixture_focus_component = light ? "forge.light" : "forge.reflection_probe";
    for (unsigned frame = 0; frame < 3; ++frame) {
        capture.new_frame();
        ImGui::SetNextWindowPos({10, 10});
        ImGui::SetNextWindowSize(light ? ImVec2{1060, 880} : ImVec2{740, 500});
        ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        inspector.draw(scene, project, id);
        ImGui::End();
        capture.render();
    }
    capture.save(root / (!light ? "local-reflection-probe-controls.png"
                         : light_kind == LightKind::Rectangle ? "rectangle-area-light-controls.png"
                         : light_kind == LightKind::Sphere    ? "sphere-area-light-controls.png"
                                                              : "soft-shadow-light-controls.png"));
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2 || (argc == 3 && (std::string_view(argv[2]) == "--capture" ||
                                          std::string_view(argv[2]) == "--capture-light" ||
                                          std::string_view(argv[2]) == "--capture-rectangle" ||
                                          std::string_view(argv[2]) == "--capture-sphere")),
              "Expected probe UI scratch root or --capture");
        if (argc == 3)
            capture(std::filesystem::absolute(argv[1]), std::string_view(argv[2]) != "--capture",
                    std::string_view(argv[2]) == "--capture-rectangle" ? LightKind::Rectangle
                    : std::string_view(argv[2]) == "--capture-sphere"  ? LightKind::Sphere
                                                                       : LightKind::Directional);
        else
            for (const auto scale : {1.f, 1.5f, 2.f})
                for (const auto size : {ImVec2{1200, 1000}, ImVec2{960, 640}})
                    exercise(std::filesystem::absolute(argv[1]), scale, size);
        std::cout << "Probe actual creation/picker/Inspector/history/guide/framing/Save PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
