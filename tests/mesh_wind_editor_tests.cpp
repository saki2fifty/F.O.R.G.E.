#include "editor/component_inspector.hpp"
#include "editor/runtime_tuning_review.hpp"
#include "imgui_input_driver.hpp"
#include "imgui_software_capture.hpp"
#include <iostream>
using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void exercise(const std::filesystem::path& scratch, float scale, bool compact,
              const std::filesystem::path& captures) {
    const auto root = scratch / AssetId::generate().str();
    std::filesystem::create_directories(root);
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root, true);
    const std::string entity =
        authoring_command(scene, "entity.create",
                          {{"recipe", "primitive.0"}, {"name", "Wind template"}})
            .at("selected");
    ComponentInspector inspector;
    inspector.fixture_focus_component = "forge.mesh_renderer";
    ui::EditorUiContext context;
    ui::ContextScope scope(context);
    const bool canonical_capture = !captures.empty() && scale == 1 && !compact;
    const unsigned width = canonical_capture ? 720 : unsigned((compact ? 460 : 850) * scale),
                   height = canonical_capture ? 840 : unsigned(900 * scale);
    test::ImGuiSoftwareCapture capture(width, height);
    ui::style(scale);
    ImGui::GetIO().ConfigInputTrickleEventQueue = false;
    test::observe_ui = true;
    bool playing = false;
    EngineContext runtime_engine(WorldRole::Runtime);
    Scene runtime(runtime_engine.world());
    RuntimeTuningReview review;
    auto live_schema = scene.schema();
    auto types = Json::array();
    for (auto type : live_schema.at("components"))
        if (type.at("id") == "forge.mesh_renderer") {
            auto fields = Json::array();
            for (const auto& field : type.at("fields"))
                if (field.at("id") == "wind")
                    fields.push_back(field);
            type["fields"] = std::move(fields);
            types.push_back(std::move(type));
        }
    live_schema["components"] = std::move(types);
    auto frame = [&] {
        test::ui_targets.clear();
        capture.new_frame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({float(width), float(height)}, ImGuiCond_Always);
        ImGui::Begin("Inspector");
        if (playing) {
            inspector.draw_runtime(
                runtime.effective_document(), live_schema, root, entity,
                [&](const std::string& type, const std::string& property, const Json& value) {
                    (void)runtime.tune_runtime_property(entity, type, property, value);
                    return true;
                },
                false,
                [&] {
                    check(review.capture(scene, project, runtime.effective_document(), live_schema,
                                         entity, 42),
                          "Wind Keep values did not capture the transient runtime property");
                });
        } else
            inspector.draw(scene, project, entity);
        ImGui::End();
        if (review.open)
            review.draw(scene, project, playing);
        capture.render();
    };
    test::ImGuiInputDriver input(frame);
    frame();
    frame();
    inspector.fixture_focus_component.clear();
    input.click("property-tree:wind");
    const auto before = scene.document();
    input.click("property-value:enabled");
    input.type("property-value:amplitude", "0.45");
    input.type("property-value:direction_z", "-2");
    input.type("property-value:frequency", "1.25");
    const auto authored = scene.document();
    const auto& wind = scene.entity(entity).get<MeshRenderer>().wind;
    check(wind.enabled && wind.amplitude == .45 && wind.direction_z == -2 && wind.frequency == 1.25,
          "Actual Wind Inspector controls did not commit reflected values");
    check(scene.undo() && scene.entity(entity).get<MeshRenderer>().wind.frequency == .5 &&
              scene.redo() && scene.document() == authored && authored != before,
          "Wind Inspector bypassed ordinary history");
    if (!captures.empty() && scale == 1 && !compact) {
        std::filesystem::create_directories(captures);
        input.click("property-tree:wind");
        input.click("property-tree:wind");
        for (const char* key :
             {"property-value:enabled", "property-value:amplitude", "property-value:height",
              "property-value:direction_x", "property-value:direction_z",
              "property-value:frequency", "property-value:wavelength", "property-value:phase"}) {
            const auto box = test::ui_targets.at(key);
            check(box.minimum.y >= box.clip_minimum.y && box.maximum.y <= box.clip_maximum.y,
                  "Authentic Wind control capture clips a required field");
        }
        capture.save(captures / "foliage-wind-inspector.png");
    }
    runtime.restore_snapshot(authored);
    playing = true;
    frame();
    frame();
    input.click("property-tree:wind");
    input.type("property-value:amplitude", "0.8");
    input.type("property-value:phase", "90");
    check(runtime.entity(entity).get<MeshRenderer>().wind.amplitude == .8 &&
              runtime.entity(entity).get<MeshRenderer>().wind.phase == 90 &&
              scene.document() == authored,
          "Live Wind Inspector changed authored intent or failed native tuning");
    if (!captures.empty() && scale == 1 && !compact)
        capture.save(captures / "foliage-wind-live.png");
    input.click("runtime:keep_values");
    check(review.pending() && review.fields().size() == 1 &&
              review.fields().front().property == "wind" && scene.document() == authored,
          "Wind runtime review did not capture one atomic property");
    playing = false;
    frame();
    input.click("runtime_review:apply");
    const auto kept = scene.document();
    check(!review.pending() && scene.entity(entity).get<MeshRenderer>().wind.amplitude == .8 &&
              scene.undo() && scene.document() == authored && scene.redo() &&
              scene.document() == kept,
          "Wind Keep values did not publish one ordinary Undo/Redo entry after Stop");
    test::observe_ui = false;
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2 || argc == 3, "Need scratch root and optional authentic capture directory");
        for (float scale : {1.f, 1.5f, 2.f})
            for (bool compact : {false, true})
                exercise(argv[1], scale, compact,
                         argc == 3 ? std::filesystem::path(argv[2]) : std::filesystem::path{});
        std::cout << "Wind Inspector/live/Keep controls and history passed six layouts\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
