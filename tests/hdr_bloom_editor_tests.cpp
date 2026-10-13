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
    std::string error;
    unsigned commits = 0;
    auto frame = [&] {
        test::ui_targets.clear();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({10, 10}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({std::min(800 * scale, display.x - 20), display.y - 20},
                                 ImGuiCond_Always);
        ImGui::Begin("Scene lighting");
        try {
            ui::scene_bloom_controls(scene_render_settings(scene.document()).bloom, locked,
                                     [&](const Json& patch) {
                                         authoring_command(scene, "scene.rendering.set", patch);
                                         ++commits;
                                         error.clear();
                                     });
        } catch (const std::exception& exception) {
            error = exception.what();
            ui::field_error(error);
        }
        ImGui::End();
        ImGui::Render();
    };
    auto click = [&](const char* key) {
        frame();
        const auto box = test::ui_targets.at(key);
        check(box.enabled && box.minimum.x >= box.clip_minimum.x &&
                  box.maximum.x <= box.clip_maximum.x && box.minimum.y >= box.clip_minimum.y &&
                  box.maximum.y <= box.clip_maximum.y,
              "Bloom control clipped or disabled");
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
    test::observe_ui = false;
    ImGui::DestroyContext();
}
} // namespace
int main() {
    try {
        for (float scale : {1.f, 1.5f, 2.f})
            for (const auto display : {ImVec2{2400, 1700}, ImVec2{960, 640}})
                interactions(scale, display);
        std::cout << "Bloom actual input, rejection, read-only and Scene Undo PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
