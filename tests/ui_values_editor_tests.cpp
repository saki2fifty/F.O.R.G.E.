#include "editor/animation_timeline.hpp"
#include "editor/component_inspector.hpp"
#include "editor/runtime_tuning_review.hpp"
#include "imgui_input_driver.hpp"
#include "imgui_software_capture.hpp"
#include <forge/ui_values.hpp>
#include <imgui_internal.h>
#include <iostream>
using namespace forge;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
void exercise(const std::filesystem::path& output, float scale) {
    const auto root = output / AssetId::generate().str();
    SceneDocument::create_project(root, "UI Values");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root);
    const auto hud = authoring_command(scene, "entity.create", {{"name", "Animated HUD"}})
                         .at("selected")
                         .get<std::string>();
    authoring_command(scene, "component.add", {{"entity", hud}, {"component", "forge.ui_values"}});
    test::ImGuiSoftwareCapture capture(int(640 * scale), int(650 * scale));
    auto& io = ImGui::GetIO();
    io.ConfigInputTrickleEventQueue = false;
    ui::style(scale);
    ui::EditorUiContext context;
    ui::ContextScope scope(context);
    context.selection.select_entity(hud);
    ComponentInspector inspector;
    RuntimeTuningReview review;
    test::observe_ui = true;
    bool live = false, timeline = false, locked = false;
    AnimationTimelineEditor editor;
    std::string message;
    Json runtime;
    unsigned submissions = 0;
    const auto schema = scene.schema();
    auto frame = [&] {
        test::ui_targets.clear();
        if (timeline)
            editor.poll(project, scene, locked, 1. / 60, message);
        capture.new_frame();
        ImGui::SetNextWindowPos({8 * scale, 8 * scale}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({624 * scale, 634 * scale}, ImGuiCond_Always);
        if (timeline) {
            editor.draw(project, scene, locked);
            capture.render();
            return;
        }
        ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        if (live)
            inspector.draw_runtime(
                runtime, schema, root, hud,
                [&](const auto& component, const auto& property, const Json& value) {
                    check(component == "forge.ui_values" && property == "values",
                          "Inspector redirected the live target");
                    ++submissions;
                    for (auto& row : runtime["entities"])
                        if (row["id"] == hud)
                            row["components"][component][property] = value;
                    return true;
                },
                false, [] {});
        else
            inspector.draw(scene, project, hud);
        ImGui::End();
        capture.render();
    };
    test::ImGuiInputDriver input(frame);
    frame();
    frame();
    input.click("property-tree:values");
    input.click("button:Add item...");
    input.click("property-tree:new-item");
    input.type("property-value:name", "amount");
    input.type("property-value:value", "0.25");
    input.click("button:Add");
    frame();
    check(scene.entity(hud).get<UiValues>().values == std::vector<UiValue>{{"amount", .25}},
          "Actual Inspector draft did not add the named number");
    input.click("property-tree:value");
    input.type("property-value:value", "0.75");
    check(scene.entity(hud).get<UiValues>().values[0].value == .75,
          "Actual numeric input did not commit");
    check(scene.undo() && scene.entity(hud).get<UiValues>().values[0].value == .25 && scene.redo(),
          "Inspector UI Values edit bypassed Scene Undo/Redo");
    frame();
    frame();
    if (scale == 1)
        capture.save(output / "ui-values-inspector.png");
    const auto authored = scene.document();
    runtime = scene.effective_document();
    live = true;
    frame();
    frame();
    check(test::ui_targets.at("runtime-field:forge.ui_values:values").enabled,
          "UI Values absent from live Inspector");
    if (!test::ui_targets.contains("property-tree:value"))
        input.click("property-tree:values");
    if (!test::ui_targets.contains("property-value:value"))
        input.click("property-tree:value");
    input.type("property-value:value", "0.4");
    frame();
    check(submissions == 1 && scene.document() == authored,
          "Live Inspector wrote authored state or duplicated submission");
    check(review.capture(scene, project, runtime, schema, hud, 42) && review.fields().size() == 1,
          "Existing tuning review omitted UI Values");
    if (scale == 1)
        capture.save(output / "ui-values-live.png");
    live = false;
    check(review.apply(scene, project, false), "Existing tuning review did not apply after Stop");
    check(scene.entity(hud).get<UiValues>().values[0].value == .4 && scene.undo() &&
              scene.document() == authored,
          "Kept UI Values failed ordinary Scene Undo");
    auto clip =
        TransformAnimationDocument::create(project.writer_guard(), "Assets/UI.animation.json");
    clip->save();
    clip.reset();
    editor.open(project, "Assets/UI.animation.json");
    editor.bind(scene, hud);
    timeline = true;
    frame();
    frame();
    input.click("animation:component_fields");
    input.click("animation:component_add:forge.ui_values:values [\"amount\"] / value");
    check(editor.document()->source().clip().component_bindings.size() == 1,
          "Actual Timeline did not add the reflected builtin leaf");
    const auto bound = editor.document()->source().document;
    input.type("animation:component_value:forge.ui_values:values [\"amount\"] / value", "0.9");
    input.click("button:Add / Update key at playhead");
    check(std::abs(
              editor.document()->source().clip().keys[0].component_values[0].value.get<double>() -
              .9) < 1e-5,
          "Timeline did not commit its named UI value");
    editor.undo();
    frame();
    check(editor.document()->source().document == bound && scene.document() == authored,
          "Timeline field edit bypassed clip Undo or changed the Scene");
    if (scale == 1)
        capture.save(output / "ui-values-timeline.png");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need UI Values fixture scratch root");
        const auto output = std::filesystem::path(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(output);
        for (float scale : {1.f, 1.5f, 2.f})
            exercise(output, scale);
        std::cout << output
                  << "\nActual UI Values Inspector draft, numeric input, Undo, live tuning and "
                     "review passed at three scales\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
