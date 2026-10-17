#include "editor/scene_lighting.hpp"
#include <imgui_internal.h>
#include <iostream>
using namespace forge;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
void interactions(float scale, ImVec2 display) {
    EngineContext engine;
    Scene scene(engine.world());
    auto extended = scene.document();
    extended["rendering"] = {{"version", 1u}, {"bloom", {{"extension", "retain"}}}};
    scene.reset(extended);
    const auto original = scene.document();
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
    bool locked = false;
    bool color_controls = false;
    bool full_owner = false;
    std::string error;
    unsigned commits = 0;
    auto frame = [&] {
        test::ui_targets.clear();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({10, 10}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({std::min(800 * scale, display.x - 20), display.y - 20},
                                 ImGuiCond_Always);
        if (full_owner) {
            bool open = true;
            ui::scene_lighting(open, scene, nullptr, locked);
        } else {
            ImGui::Begin("Scene lighting");
            try {
                const auto settings = scene_render_settings(scene.document());
                const auto commit = [&](const Json& patch) {
                    authoring_command(scene, "scene.rendering.set", patch);
                    ++commits;
                    error.clear();
                };
                if (color_controls)
                    ui::scene_color_controls(settings.color_adjustments, locked, commit);
                else
                    ui::scene_bloom_controls(settings.bloom, locked, commit);
            } catch (const std::exception& exception) {
                error = exception.what();
                ui::field_error(error);
            }
            ImGui::End();
        }
        ImGui::Render();
    };
    auto click = [&](const char* key) {
        test::UiTarget box;
        bool visible = false;
        for (unsigned attempt = 0; attempt < 20; ++attempt) {
            frame();
            box = test::ui_targets.at(key);
            if (box.minimum.y >= box.clip_minimum.y && box.maximum.y <= box.clip_maximum.y) {
                visible = true;
                break;
            }
            io.AddMousePosEvent(box.clip_minimum.x + 8, box.clip_maximum.y - 8);
            io.AddMouseWheelEvent(0, box.minimum.y < box.clip_minimum.y ? .5f : -.5f);
            frame();
        }
        check(visible, "Display control could not be reached by scrolling");
        if (!box.enabled || box.minimum.x < box.clip_minimum.x ||
            box.maximum.x > box.clip_maximum.x || box.minimum.y < box.clip_minimum.y ||
            box.maximum.y > box.clip_maximum.y)
            throw std::runtime_error(
                std::string(key) + " clipped/disabled at scale " + std::to_string(scale) + ": " +
                Json{{"box", {box.minimum.x, box.minimum.y, box.maximum.x, box.maximum.y}},
                     {"clip",
                      {box.clip_minimum.x, box.clip_minimum.y, box.clip_maximum.x,
                       box.clip_maximum.y}},
                     {"enabled", box.enabled}}
                    .dump());
        io.AddMousePosEvent((box.minimum.x + box.maximum.x) / 2,
                            (box.minimum.y + box.maximum.y) / 2);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
    };
    auto type = [&](const char* key, const char* value) {
        click(key);
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        io.AddKeyEvent(ImGuiKey_A, true);
        frame();
        io.AddKeyEvent(ImGuiKey_A, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        io.AddInputCharactersUTF8(value);
        frame();
        io.AddKeyEvent(ImGuiKey_Enter, true);
        frame();
        io.AddKeyEvent(ImGuiKey_Enter, false);
        frame();
    };
    for (unsigned i = 0; i < 4; ++i)
        frame();
    click("bloom:enabled");
    check(commits == 1 && scene_render_settings(scene.document()).bloom.enabled,
          "Bloom checkbox did not commit");
    check(scene.undo() && scene.document() == original && scene.redo(),
          "Bloom checkbox Undo failed");
    type("bloom:threshold", "2.5");
    type("bloom:intensity", "1.5");
    type("bloom:radius", "3.25");
    check(commits == 4 &&
              scene_render_settings(scene.document()).bloom ==
                  SceneBloom{true, 2.5f, 1.5f, 3.25f} &&
              scene.document()["rendering"]["bloom"]["extension"] == "retain",
          "Typed bloom fields did not preserve intent/extensions");
    const auto good = scene.document();
    type("bloom:radius", "100");
    check(!error.empty() && commits == 4 && scene.document() == good,
          "Invalid typed bloom settings changed Scene");
    // Rejection unwinds nested disabled/ID stacks; subsequent valid interaction works.
    type("bloom:radius", "4");
    check(error.empty() && commits == 5 &&
              scene_render_settings(scene.document()).bloom.radius == 4,
          "Bloom UI did not recover after rejected input");
    check(scene.undo() && scene.document() == good && scene.redo(), "Bloom field Undo failed");
    locked = true;
    frame();
    for (const char* key : {"bloom:enabled", "bloom:threshold", "bloom:intensity", "bloom:radius"})
        check(!test::ui_targets.at(key).enabled, "Bloom authored control enabled during Play");
    locked = false;
    color_controls = true;
    const auto before_color = scene.document();
    click("color:enabled");
    check(scene_render_settings(scene.document()).color_adjustments.enabled,
          "Color checkbox did not enable authored grading");
    type("color:saturation", "0");
    type("color:contrast", "1.5");
    type("color:filter", "0.5"); // Center lane of the real RGB field is green.
    const auto adjusted = scene.document();
    check(scene_render_settings(adjusted).color_adjustments ==
                  SceneColorAdjustments{true, {1, .5f, 1}, 0, 1.5f} &&
              adjusted["rendering"]["bloom"] == before_color["rendering"]["bloom"],
          "Typed color controls lost values or changed bloom");
    type("color:saturation", "3");
    check(!error.empty() && scene.document() == adjusted,
          "Rejected color input changed authored values");
    type("color:saturation", "0.5");
    check(error.empty() &&
              scene_render_settings(scene.document()).color_adjustments.saturation == .5f,
          "Color UI did not recover after rejected input");
    const auto before_reset = scene.document();
    click("color:reset");
    check(scene_render_settings(scene.document()).color_adjustments == SceneColorAdjustments{} &&
              scene.undo() && scene.document() == before_reset && scene.redo(),
          "Reset color adjustments did not use one reversible Scene operation");
    locked = true;
    frame();
    for (const char* key :
         {"color:enabled", "color:filter", "color:saturation", "color:contrast", "color:reset"})
        check(!test::ui_targets.at(key).enabled, "Authored color control enabled during Play");
    // Exercise the real Scene lighting owner, including its Problems integration,
    // rather than inferring recovery from the callback used above.
    locked = false;
    full_owner = true;
    context.task.focus_document("material", "Material");
    type("color:contrast", "3");
    check(context.problems.size() == 1 &&
              context.problems.items().front().key == "scene.lighting" &&
              !context.problems.items().front().occurred_at.empty(),
          "Actual Scene lighting rejection did not create a timestamped Problem");
    type("color:contrast", "1.25");
    check(context.problems.size() == 0 &&
              scene_render_settings(scene.document()).color_adjustments.contrast == 1.25f &&
              context.task.owner == ui::DocumentTask::Scene,
          "Corrected Scene lighting left a stale Problem or routed Save/history to an asset");
    test::observe_ui = false;
    ImGui::DestroyContext();
}
} // namespace
int main() {
    try {
        for (float scale : {1.f, 1.5f, 2.f})
            for (const auto display : {ImVec2{2400, 1700}, ImVec2{960, 640}})
                interactions(scale, display);
        std::cout
            << "Bloom/color actual input, rejection/recovery, read-only and Scene Undo PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
