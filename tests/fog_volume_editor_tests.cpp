#include "editor/component_inspector.hpp"
#include "editor/framing.hpp"
#include "editor/runtime_tuning_review.hpp"
#include "editor/spatial_helpers.hpp"
#include "imgui_input_driver.hpp"
#include "imgui_software_capture.hpp"
#include <iostream>
using namespace forge;
namespace {
void check(bool condition, const char* why) {
    if (!condition)
        throw std::runtime_error(why);
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
                          {{"recipe", "render.fog_volume"}, {"name", "Courtyard mist"}})
            .at("selected");
    const auto initial = scene.document();
    check(scene.entity(entity).get<FogVolume>() == FogVolume{}, "Fog recipe lost schema defaults");
    const auto initial_markers = ui::spatial_markers(scene.effective_document(), 96, 64, 10);
    check(initial_markers.size() == 1 && initial_markers[0].fog &&
              initial_markers[0].lines.size() == 12,
          "Box Fog guide did not produce twelve edges");
    ComponentInspector inspector;
    inspector.fixture_focus_component = "forge.fog_volume";
    ui::EditorUiContext context;
    ui::ContextScope scope(context);
    const unsigned width = unsigned((compact ? 460 : 720) * scale);
    const unsigned height = unsigned(900 * scale);
    test::ImGuiSoftwareCapture capture(width, height);
    ui::style(scale);
    ImGui::GetIO().ConfigInputTrickleEventQueue = false;
    test::observe_ui = true;
    EngineContext runtime_engine(WorldRole::Runtime);
    Scene runtime(runtime_engine.world());
    RuntimeTuningReview review;
    auto schema = scene.schema();
    Json types = Json::array();
    for (const auto& type : schema.at("components"))
        if (type.at("id") == "forge.fog_volume")
            types.push_back(type);
    schema["components"] = types;
    bool playing = false;
    auto frame = [&] {
        test::ui_targets.clear();
        capture.new_frame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({float(width), float(height)}, ImGuiCond_Always);
        ImGui::Begin("Inspector");
        if (playing)
            inspector.draw_runtime(
                runtime.effective_document(), schema, root, entity,
                [&](const std::string& type, const std::string& property, const Json& value) {
                    runtime.tune_runtime_property(entity, type, property, value);
                    return true;
                },
                false,
                [&] {
                    check(review.capture(scene, project, runtime.effective_document(), schema,
                                         entity, 7),
                          "Fog review capture failed");
                });
        else
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
    input.click("component-field:forge.fog_volume:shape");
    input.click("property:choice:shape:1");
    input.type("component-field:forge.fog_volume:density", "0.25");
    input.type("component-field:forge.fog_volume:half_extent_x", "3");
    const auto authored = scene.document();
    check(scene.entity(entity).get<FogVolume>().shape == 1 &&
              scene.entity(entity).get<FogVolume>().density == .25 &&
              scene.entity(entity).get<FogVolume>().half_extent_x == 3,
          "Fog Inspector shape or numeric controls failed");
    check(authored != initial && scene.undo() &&
              scene.entity(entity).get<FogVolume>().half_extent_x == 2 && scene.redo() &&
              scene.document() == authored,
          "Fog Inspector bypassed history");
    const auto effective = scene.effective_document();
    const auto markers = ui::spatial_markers(effective, 96, 64, 10);
    check(markers.size() == 1 && markers[0].fog && markers[0].lines.size() == 144,
          "Ellipsoid guide does not contain the three transformed rings");
    const auto bounds =
        include_selected_helper_positions(effective, {EntityId::parse(entity)}, {}, {});
    check(bounds.minimum[0] == -3 && bounds.maximum[0] == 3 && bounds.maximum[1] == 3,
          "Fog framing omitted its transformed extent");
    if (!captures.empty() && scale == 1 && !compact) {
        std::filesystem::create_directories(captures);
        capture.save(captures / "fog-volume-inspector.png");
    }
    runtime.restore_snapshot(authored);
    playing = true;
    frame();
    frame();
    input.type("runtime-field:forge.fog_volume:density", "0.5");
    check(runtime.entity(entity).get<FogVolume>().density == .5 && scene.document() == authored,
          "Live Fog Inspector missed native tuning or changed source");
    input.click("runtime:keep_values");
    check(review.pending() && review.fields().size() == 1 &&
              review.fields()[0].property == "density",
          "Fog Keep values captured wrong fields");
    playing = false;
    frame();
    input.click("runtime_review:apply");
    check(scene.entity(entity).get<FogVolume>().density == .5 && scene.undo() &&
              scene.document() == authored,
          "Kept Fog tuning bypassed authored Undo");
    test::observe_ui = false;
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2 || argc == 3, "Need scratch and optional capture directory");
        for (float scale : {1.f, 1.5f, 2.f})
            for (bool compact : {false, true})
                exercise(argv[1], scale, compact, argc == 3 ? argv[2] : "");
        std::cout << "Fog Inspector, live tuning, review, guides and framing passed six layouts\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
