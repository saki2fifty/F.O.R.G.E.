#include "editor/component_inspector.hpp"
#include <backends/imgui_impl_sdlrenderer3.h>
#include <forge/input_message.hpp>
#include <iostream>
#include <memory>

namespace {
using namespace forge;
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void exercise(const std::filesystem::path& output, float scale) {
    const auto root = output / ("project-" + std::to_string(scale));
    SceneDocument::create_project(root, "Input Messages");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root);
    auto settings = project.settings().document();
    const auto action = ActionId::generate().str();
    const auto axis = ActionId::generate().str(), control = ActionId::generate().str();
    settings["input"] = {
        {"version", 2},
        {"contexts", Json::array({{{"name", "Fixed"}, {"active", true}},
                                  {{"name", "Menu"}, {"active", true}, {"phase", "control"}}})},
        {"actions", Json::array({{{"id", action},
                                  {"name", "Send message"},
                                  {"kind", "digital"},
                                  {"context", "Fixed"},
                                  {"bindings", Json::array({{{"control", "key.space"}}})}},
                                 {{"id", axis},
                                  {"name", "Axis value"},
                                  {"kind", "axis1"},
                                  {"context", "Fixed"},
                                  {"bindings", Json::array()}},
                                 {{"id", control},
                                  {"name", "Menu excluded"},
                                  {"kind", "digital"},
                                  {"context", "Menu"},
                                  {"bindings", Json::array()}}})}};
    project.save_settings(settings);
    scene.replace({{"version", 1},
                   {"entities",
                    Json::array({{{"id", "source"},
                                  {"name", "Message source"},
                                  {"components",
                                   {{"forge.tint", {{"r", .2}, {"g", .3}, {"b", .4}}},
                                    {"forge.input_message",
                                     {{"enabled", true}, {"action", ""}, {"payload", ""}}}}}}})}});
    const auto entity = scene.canonical_id("source");
    const std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(
        SDL_CreateSurface(int(600 * scale), int(500 * scale), SDL_PIXELFORMAT_RGBA32),
        SDL_DestroySurface);
    check(bool(surface), SDL_GetError());
    const std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(
        SDL_CreateSoftwareRenderer(surface.get()), SDL_DestroyRenderer);
    check(bool(renderer), SDL_GetError());
    const std::unique_ptr<ImGuiContext, decltype(&ImGui::DestroyContext)> imgui(
        ImGui::CreateContext(), ImGui::DestroyContext);
    check(ImGui_ImplSDLRenderer3_Init(renderer.get()), "ImGui renderer unavailable");
    struct Backend {
        ~Backend() { ImGui_ImplSDLRenderer3_Shutdown(); }
    } backend;
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {600 * scale, 500 * scale};
    io.DeltaTime = 1.f / 60;
    io.ConfigInputTrickleEventQueue = false;
    test::observe_ui = true;
    ui::style(scale);
    ComponentInspector inspector;
    auto frame = [&] {
        test::ui_targets.clear();
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({8 * scale, 8 * scale});
        ImGui::SetNextWindowSize({580 * scale, 480 * scale});
        ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
        inspector.draw(scene, project, entity);
        ImGui::End();
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer.get(), 12, 14, 18, 255);
        SDL_RenderClear(renderer.get());
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer.get());
        SDL_RenderPresent(renderer.get());
    };
    auto click = [&](const std::string& key) {
        const auto target = test::ui_targets.at(key);
        const float x = (target.minimum.x + target.maximum.x) / 2,
                    y = (target.minimum.y + target.maximum.y) / 2;
        check(target.enabled && x >= target.clip_minimum.x && x <= target.clip_maximum.x &&
                  y >= target.clip_minimum.y && y <= target.clip_maximum.y,
              "Message control clipped or disabled");
        io.AddMousePosEvent(x, y);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
    };
    frame();
    frame();
    click("input-message-choice:action");
    frame();
    unsigned options = 0;
    for (const auto& [key, target] : test::ui_targets)
        options += key.starts_with("input-message-option:action:");
    check(options == 1, "Message action selector admitted axes or control-only contexts");
    click("input-message-option:action:" + action);
    frame();
    check(scene.entity(entity).get<InputMessage>().action == action,
          "Action selector lost stable ID");
    click("input-message-choice:payload");
    frame();
    check(!test::ui_targets.contains("input-message-option:payload:forge.input_message"),
          "Emitter can select itself");
    click("input-message-option:payload:forge.tint");
    frame();
    check(scene.entity(entity).get<InputMessage>().payload == "forge.tint",
          "Payload selector lost component key");
    check(scene.undo() && scene.entity(entity).get<InputMessage>().payload.empty() && scene.redo(),
          "Actual Inspector selection bypassed Undo/Redo");
    frame();
    click("input-message-choice:trigger");
    frame();
    click("input-message-option:trigger:2");
    check(scene.entity(entity).get<InputMessage>().trigger == 2 && scene.undo() &&
              scene.entity(entity).get<InputMessage>().trigger == 0 && scene.redo(),
          "Changed trigger selection lost its value or one-step history");
    frame();
    click("input-message-choice:action");
    frame();
    check(test::ui_targets.contains("input-message-option:action:" + axis) &&
              !test::ui_targets.contains("input-message-option:action:" + control),
          "Changed selector omitted analog actions or admitted control-frame actions");
    click("input-message-option:action:" + axis);
    check(scene.entity(entity).get<InputMessage>().action == axis,
          "Analog action selection lost its stable ID");
    frame();
    click("input-message-choice:trigger");
    frame();
    click("input-message-option:trigger:0");
    check(scene.entity(entity).get<InputMessage>().action == axis,
          "Changing trigger silently retargeted an incompatible action");
    check(scene.undo() && scene.entity(entity).get<InputMessage>().trigger == 2,
          "Trigger Undo did not restore Changed mode");
    frame();
    frame();
    if (scale == 1)
        check(SDL_SavePNG(surface.get(), (output / "input-message-inspector.png").string().c_str()),
              SDL_GetError());
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Provide build-owned UI scratch");
        const auto output = std::filesystem::path(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(output);
        for (float scale : {1.f, 1.5f, 2.f})
            exercise(output, scale);
        std::cout << output << "\nActual Inspector selectors and history passed at three scales\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
