#include "editor/component_inspector.hpp"
#include "editor/input_message_monitor.hpp"
#include "imgui_software_capture.hpp"
#include <backends/imgui_impl_sdlrenderer3.h>
#include <forge/input_message.hpp>
#include <forge/runtime.hpp>
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
void monitor(const std::filesystem::path& output, float scale, int width, int height) {
    EngineContext engine(WorldRole::Runtime);
    Scene scene(engine.world());
    const auto action = ActionId::generate().str();
    scene.replace(
        {{"version", 1},
         {"entities",
          Json::array(
              {{{"id", "source"},
                {"name", "Tint sender"},
                {"components",
                 {{"forge.tint", {{"r", .2}, {"g", .3}, {"b", .4}}},
                  {"forge.input_message",
                   {{"enabled", true}, {"action", action}, {"payload", "forge.tint"}}}}}}})}});
    const auto source = scene.canonical_id("source");
    const auto authored = scene.document();
    RuntimeSimulation runtime(engine.world(), scene);
    runtime.input().configure(InputMap(
        {{"version", 1},
         {"actions", Json::array({{{"id", action},
                                   {"name", "Send tint"},
                                   {"kind", "digital"},
                                   {"bindings", Json::array({{{"control", "key.space"}}})}}})}}));
    for (const auto value : {1., 0., 1.}) {
        runtime.input().submit({{"key.space", value}});
        runtime.tick(1.f / 60);
    }
    const auto snapshot = runtime.input_status();
    const auto& messages = snapshot.at("messages");
    const Json* display = &messages;
    check(messages.at("sources").at(0).at("emitted_total").get<unsigned>() == 2,
          "Monitor fixture did not dispatch through the actual native owner");
    test::ImGuiSoftwareCapture capture(int(width * scale), int(height * scale));
    auto& io = ImGui::GetIO();
    io.FontGlobalScale = scale;
    io.ConfigInputTrickleEventQueue = false;
    ImGui::GetStyle().ScaleAllSizes(scale);
    test::observe_ui = true;
    const auto frame = [&] {
        test::ui_targets.clear();
        capture.new_frame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Input messages", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoSavedSettings);
        ui::draw_input_messages(*display);
        ImGui::End();
        capture.render();
    };
    frame();
    frame();
    const auto target = test::ui_targets.at("input-message-monitor:" + source);
    const float x = (target.minimum.x + target.maximum.x) * .5f,
                y = (target.minimum.y + target.maximum.y) * .5f;
    check(target.enabled && x >= target.clip_minimum.x && x <= target.clip_maximum.x &&
              y >= target.clip_minimum.y && y <= target.clip_maximum.y,
          "Actual message source control was clipped or disabled");
    io.AddMousePosEvent(x, y);
    frame();
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();
    check(test::ui_targets.contains("input-message-preview:" + source),
          "Actual message monitor did not expand its native payload preview");
    auto legacy = messages;
    legacy.erase("preview_limit_bytes");
    for (auto& row : legacy["sources"])
        for (const auto* field : {"name", "action_name", "emitted_total", "last_sent"})
            row.erase(field);
    display = &legacy;
    frame();
    check(test::ui_targets.contains("input-message-inspection-unavailable:" + source) &&
              !test::ui_targets.contains("input-message-preview:" + source),
          "Older runtime observations were displayed as a new empty inspection");
    display = &messages;
    frame();
    frame();
    check(runtime.input_status() == snapshot && scene.document() == authored,
          "Read-only monitor advanced time or changed authored history");
    if (scale == 1 && width == 820)
        capture.save(output / "input-message-monitor.png");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Provide build-owned UI scratch");
        const auto output = std::filesystem::path(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(output);
        for (float scale : {1.f, 1.5f, 2.f}) {
            exercise(output, scale);
            monitor(output, scale, 820, 560);
            monitor(output, scale, 520, 440);
        }
        std::cout << output << "\nActual Inspector selectors and history passed at three scales\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
