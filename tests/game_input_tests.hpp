#pragma once
#include "game_input.hpp"
#include "project_settings.hpp"
void require(bool condition, const char* message);
inline void test_game_input(const char* runtime) {
    using namespace forge;
    require(SDL_InitSubSystem(SDL_INIT_GAMEPAD), SDL_GetError());
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = 6;
    desc.nbuttons = 15;
    desc.axis_mask = 63;
    desc.button_mask = 32767;
    desc.name = "FORGE test gamepad";
    const auto device = SDL_AttachVirtualJoystick(&desc);
    require(device != 0, SDL_GetError());
    {
        PlaySession play;
        const auto action = ActionId::generate();
        play.configure(
            90,
            InputMap({{"version", 1},
                      {"actions",
                       Json::array({{{"id", action},
                                     {"name", "Test"},
                                     {"kind", "digital"},
                                     {"bindings", Json::array({{{"control", "key.space"}},
                                                               {{"control", "pad.south"}}})}}})}}));
        play.start(runtime, empty_scene(), {}, true);
        auto wait = [&](auto done) {
            const auto deadline = SDL_GetTicks() + 5000;
            while (!done() && play.active() && SDL_GetTicks() < deadline) {
                play.pump();
                SDL_Delay(1);
            }
            require(play.active() && done(),
                    ("Input controller test: " + play.status() + " " + play.log()).c_str());
        };
        wait([&] { return play.control_ready(); });
        require(play.paused() && play.timing()["simulation_hz"] == 90,
                "Project Hz not passed through PlaySession");
        GameInput input;
        input.pump(play, true);
        input.capture(play);
        auto tick = [&] {
            wait([&] { return play.control_ready(); });
            const auto before = play.timing().at("tick").get<std::uint64_t>();
            play.step();
            wait([&] { return play.timing().at("tick").get<std::uint64_t>() > before; });
            return play.input_status().at("actions")[0];
        };
        SDL_Event e{};
        e.type = SDL_EVENT_KEY_DOWN;
        e.key.scancode = SDL_SCANCODE_SPACE;
        e.key.down = true;
        require(input.event(e, play), "Captured key leaked to editor");
        auto state = tick();
        require(state["held"] == true && state["presses"] == 1, "SDL key did not become action");
        e.key.repeat = true;
        input.event(e, play);
        require(tick()["presses"] == 1, "Key repeat repeated press edge");
        input.pump(play, false);
        require(!input.captured(), "Text/focus gate did not release capture");
        require(!input.event(e, play), "Editor-owned key was consumed by gameplay");
        state = tick();
        require(state["held"] == false && state["releases"] == 1,
                "Capture loss failed neutral input");
        input.capture(play);
        e = {};
        e.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        e.gbutton.which = device;
        e.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
        e.gbutton.down = true;
        require(input.event(e, play), "Virtual gamepad event not routed");
        state = tick();
        require(state["held"] == true && state["presses"] == 2,
                "Virtual gamepad did not map to action");
        e = {};
        e.type = SDL_EVENT_GAMEPAD_REMOVED;
        e.gdevice.which = device;
        input.event(e, play);
        require(tick()["held"] == false, "Gamepad disconnect left held action");
        e = {};
        e.type = SDL_EVENT_WINDOW_FOCUS_LOST;
        input.event(e, play);
        require(!input.captured(), "Window focus loss retained capture");
        input.viewport({100, 100}, {400, 300});
        input.capture(play);
        e = {};
        e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        e.button.x = 50;
        e.button.y = 50;
        e.button.button = SDL_BUTTON_LEFT;
        require(!input.event(e, play) && !input.captured(),
                "Editor click outside Game did not release capture and reach editor");
        require(!tick()["held"].get<bool>(), "Outside click left gameplay input held");
        play.stop();
    }
    require(SDL_DetachVirtualJoystick(device), SDL_GetError());
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
}
inline void test_project_settings_ui() {
    using namespace forge;
    const auto root =
        std::filesystem::current_path() / ("settings-ui-" + AssetId::generate().str());
    SceneDocument::create_project(root, "Settings test");
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() {
            std::error_code e;
            std::filesystem::remove_all(root, e);
        }
    } cleanup{root};
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument document(scene);
    document.open_project(root);
    auto settings = document.settings().document();
    settings["simulation_hz"] = 75;
    document.save_settings(settings);
    document.open_project(root);
    require(document.settings().simulation_hz() == 75, "Editor project setting reopen failed");
    settings["input"]["actions"].push_back(
        {{"id", ActionId::generate()},
         {"name", "Test action"},
         {"kind", "digital"},
         {"bindings", Json::array({{{"control", "key.space"}}})}});
    document.save_settings(settings);
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1280, 900};
    io.DeltaTime = 1.f / 60;
    unsigned char* pixels;
    int w, h;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    ProjectSettingsEditor editor;
    editor.open();
    std::string status;
    for (float scale : {.65f, 1.f, 2.f}) {
        ui::style(scale);
        ImGui::NewFrame();
        editor.draw(document, scene, false, status);
        ImGui::Render();
        require(ImGui::GetDrawData()->TotalVtxCount > 0, "Project settings UI empty");
    }
    ImGui::DestroyContext();
    settings["startup_scene"] = nullptr;
    document.save_settings(settings);
    document.open_project(root);
    require(document.path().empty(), "Project without startup did not open an untitled scene");
}
