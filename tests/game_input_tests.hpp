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
    {
        const auto first = SDL_AttachVirtualJoystick(&desc);
        const auto second = SDL_AttachVirtualJoystick(&desc);
        require(first && second, SDL_GetError());
        SdlGamepads pads;
        pads.discover();
        require(pads.count() >= 2, "Multiple gamepads were not discovered");
        SDL_Event event{};
        event.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
        event.gaxis.which = second;
        event.gaxis.axis = SDL_GAMEPAD_AXIS_LEFTX;
        event.gaxis.value = 100;
        require(pads.event(event, true, 1000).empty(), "Inactive stick noise stole device");
        event.gaxis.value = -32768;
        const auto switched = pads.event(event, true, 1000);
        require(pads.active() == second && switched.size() == 2 && switched[0].reset &&
                    switched[1].value == -1,
                "Deliberate device switch did not neutralize previous input");
        event.gaxis.which = first;
        require(pads.event(event, true, 1100).empty(), "Device switch guard failed");
        event = {};
        event.type = SDL_EVENT_GAMEPAD_REMOVED;
        event.gdevice.which = second;
        require(SDL_DetachVirtualJoystick(second), SDL_GetError());
        const auto removed = pads.event(event, false, 1200);
        require(removed.size() == 1 && removed[0].reset && pads.active() != second,
                "Removal retained stale handle or held input");
        const auto reconnected = SDL_AttachVirtualJoystick(&desc);
        require(reconnected && reconnected != second, "Reconnect did not get new runtime ID");
        event.type = SDL_EVENT_GAMEPAD_ADDED;
        event.gdevice.which = reconnected;
        pads.event(event, false, 1300);
        event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        event.gbutton.which = reconnected;
        event.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
        event.gbutton.down = true;
        require(pads.event(event, true, 1500).size() == 2 && pads.active() == reconnected,
                "Reconnected gamepad could not take over");
        require(std::string(pads.activity()) == "Gamepad",
                "Deliberate pad input did not select prompts");
        event = {};
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.down = true;
        pads.event(event, true, 1800);
        require(std::string(pads.activity()) == "KeyboardMouse",
                "Keyboard did not take prompt ownership");
        event = {};
        event.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
        event.gaxis.which = reconnected;
        event.gaxis.axis = SDL_GAMEPAD_AXIS_LEFTX;
        event.gaxis.value = 200;
        pads.event(event, true, 2100);
        require(std::string(pads.activity()) == "KeyboardMouse",
                "Analog noise changed prompt ownership");
        event.gaxis.value = 32767;
        pads.event(event, true, 2200);
        require(std::string(pads.activity()) == "Gamepad",
                "Deliberate axis did not select prompts");
        event = {};
        event.type = SDL_EVENT_MOUSE_MOTION;
        event.motion.xrel = 4;
        pads.event(event, true, 2250);
        require(std::string(pads.activity()) == "Gamepad", "Prompt ownership debounce failed");
        require(SDL_DetachVirtualJoystick(first), SDL_GetError());
        require(SDL_DetachVirtualJoystick(reconnected), SDL_GetError());
    }
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
}
// Regression for the checked release path: hold a key, observe the
// runtime reports held=true, then release_checked(play, true) MUST
// (a) keep the routing flag alive (keep_routing=true) AND (b)
// neutralize the previously-held action in the next runtime tick,
// because the helper ships a neutral edge before any subsequent
// gameplay edge. The original release(play) void path cleared
// routing silently; this test pins the documented contract.
//
// Runs through the same PlaySession/runtime/input action setup the
// existing gamepad suite uses (no manufactured pass): a real
// runtime is started with a single Space-bound digital action, a
// real fixed-tick step drains the input edge, and the action's
// `held` field is the source of truth.
//
// Keyboard only — Linux's cached SDL ships with the joystick
// subsystem disabled; the Windows full SDL run will exercise the
// gamepad suite above. The platform split lives in the driver
// that compiles this header, not here.
inline void test_game_input_checked_release(const char* runtime) {
    using namespace forge;
    PlaySession play;
    const auto action = ActionId::generate();
    play.configure(
        90, InputMap({{"version", 1},
                      {"actions",
                       Json::array({{{"id", action},
                                     {"name", "Test"},
                                     {"kind", "digital"},
                                     {"bindings", Json::array({{{"control", "key.space"}}})}}})}}));
    play.start(runtime, empty_scene(), {}, true);
    auto wait = [&](auto done) {
        const auto deadline = SDL_GetTicks() + 5000;
        while (!done() && play.active() && SDL_GetTicks() < deadline) {
            play.pump();
            SDL_Delay(1);
        }
        require(play.active() && done(),
                ("Checked release regression: " + play.status() + " " + play.log()).c_str());
    };
    auto tick = [&] {
        wait([&] { return play.control_ready(); });
        const auto before = play.timing().at("tick").get<std::uint64_t>();
        play.step();
        wait([&] { return play.timing().at("tick").get<std::uint64_t>() > before; });
        return play.input_status().at("actions")[0];
    };
    wait([&] { return play.control_ready(); });
    GameInput input;
    input.pump(play, true);
    // Non-relative capture is legal on the headless menu path; the
    // runtime still sees the captured flag through the input_event
    // edge submitted on capture.
    input.capture(play);
    require(input.captured(), "Capture did not set the routing flag");
    // Press space — held=true on the next fixed tick.
    SDL_Event e{};
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.scancode = SDL_SCANCODE_SPACE;
    e.key.down = true;
    require(input.event(e, play), "Captured key leaked to editor");
    require(tick().at("held").get<bool>() == true, "Space key down did not produce a held action");
    // Keep-routing checked release: routing stays alive, the
    // helper ships a neutral edge so the NEXT runtime tick reports
    // held=false for the previously-held action.
    require(input.release_checked(play, true), "Checked release with keep_routing=true failed");
    require(input.captured(), "release_checked(keep_routing=true) dropped logical routing");
    require(tick().at("held").get<bool>() == false,
            "Checked release did not neutralize the previously-held action");
    // Final release tears routing down as expected.
    input.release(play);
    require(!input.captured(), "Final release did not clear routing");
    play.stop();
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
