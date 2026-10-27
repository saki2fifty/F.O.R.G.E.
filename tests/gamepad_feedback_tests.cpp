#include "editor/sdk_platform_effects.hpp"
#include <SDL3/SDL.h>
#include <forge/project.hpp>
#include <fstream>
#include <iostream>
#include <limits>
#include <source_location>
using namespace forge;
namespace {
void check(bool value, const char* reason,
           const std::source_location location = std::source_location::current()) {
    if (!value)
        throw std::runtime_error(std::string(location.function_name()) + ":" +
                                 std::to_string(location.line()) + ": " + reason);
}
template <class F> void rejects(F run) {
    try {
        run();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Expected feedback rejection");
}
struct Feedback {
    unsigned calls = 0;
    Uint16 low = 0, high = 0;
    bool fail = false;
};
bool SDLCALL rumble(void* pointer, Uint16 low, Uint16 high) {
    auto& state = *static_cast<Feedback*>(pointer);
    ++state.calls;
    if (state.fail) {
        SDL_SetError("virtual feedback refused");
        return false;
    }
    state.low = low;
    state.high = high;
    return true;
}
SDL_JoystickID attach(Feedback* state) {
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = 6;
    desc.nbuttons = 15;
    desc.axis_mask = 63;
    desc.button_mask = 32767;
    desc.name = "FORGE feedback fixture";
    desc.userdata = state;
    desc.Rumble = state ? rumble : nullptr;
    const auto id = SDL_AttachVirtualJoystick(&desc);
    check(id != 0, SDL_GetError());
    return id;
}
void values_and_devices() {
    GamepadRumble retained{.25, .5, 200};
    for (const auto& json : {Json{{"low", true}, {"high", 0}, {"duration_ms", 100}},
                             Json{{"low", 1}, {"high", 0}, {"duration_ms", 0}},
                             Json{{"low", -1}, {"high", 0}, {"duration_ms", 10}},
                             Json{{"low", 0}, {"high", 2}, {"duration_ms", 10}},
                             Json{{"low", 0}, {"high", 0}, {"duration_ms", -1}},
                             Json{{"low", 0}, {"high", 0}, {"duration_ms", 1.5}},
                             Json{{"low", 0}, {"high", 0}, {"duration_ms", 5001}},
                             Json{{"low", 0}, {"high", 0}, {"duration_ms", true}}}) {
        rejects([&] { from_json(json, retained); });
        check(retained.low == .25 && retained.high == .5 && retained.duration_ms == 200,
              "Invalid feedback partially changed its value");
    }
    for (const auto value :
         {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
        rejects([&] { GamepadRumble{value, 0, 100}.validate(); });
    SdlGamepads pads;
    rejects([&] { pads.rumble({1, 0, 5000}); });
    pads.rumble({}); // Stopping a missing device is idempotent.
    Feedback first, second;
    const auto a = attach(&first), b = attach(&second);
    pads.discover();
    SDL_Event select{};
    select.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
    select.gaxis.which = a;
    select.gaxis.axis = SDL_GAMEPAD_AXIS_LEFTX;
    select.gaxis.value = 30000;
    pads.event(select, true, 1000);
    pads.rumble({.5, 1, 5000});
    check(first.low == 32768 && first.high == 65535, "SDL strength conversion failed");
    pads.rumble({1, .25, 1000});
    check(first.low == 65535 && first.high == 16384, "Replacement feedback failed");
    SDL_Event event{};
    event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    event.gbutton.which = b;
    event.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
    event.gbutton.down = true;
    pads.event(event, true, 1500);
    check(first.low == 0 && first.high == 0 && pads.active() == b,
          "Device switching left feedback on the old controller");
    pads.rumble({1, 1, 200});
    check(second.low == 65535, "New active device did not receive feedback");
    second.fail = true;
    check(!pads.stop_rumble() && !pads.stop_rumble() && second.low == 65535,
          "Failed native Stop was reported as physical success");
    second.fail = false;
    check(pads.stop_rumble() && second.low == 0, "Explicit feedback stop/retry failed");
    second.fail = true;
    rejects([&] { pads.rumble({1, 1, 100}); });
    second.fail = false;
    pads.rumble({1, 1, 100});
    event.type = SDL_EVENT_GAMEPAD_REMOVED;
    event.gdevice.which = b;
    pads.event(event, false, 1600);
    check(second.low == 0, "Removal did not stop owned feedback before closing");
    check(SDL_DetachVirtualJoystick(b) && SDL_DetachVirtualJoystick(a), SDL_GetError());
    event.type = SDL_EVENT_GAMEPAD_REMOVED;
    event.gdevice.which = a;
    pads.event(event, false, 1700);
    const auto unsupported = attach(nullptr);
    event.type = SDL_EVENT_GAMEPAD_ADDED;
    event.gdevice.which = unsupported;
    pads.event(event, false, 1800);
    bool denied = false;
    try {
        pads.rumble({1, 1, 100});
    } catch (const std::exception& e) {
        denied = std::string(e.what()).find("does not support") != std::string::npos;
    }
    check(denied, "Unsupported virtual controller did not report its capability failure");
    check(SDL_DetachVirtualJoystick(unsupported), SDL_GetError());
    Feedback teardown;
    const auto c = attach(&teardown);
    {
        SdlGamepads scoped;
        scoped.discover();
        event.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
        event.gaxis.which = c;
        event.gaxis.axis = SDL_GAMEPAD_AXIS_LEFTX;
        event.gaxis.value = 30000;
        scoped.event(event, true, 2000);
        scoped.rumble({1, 1, 5});
        const auto deadline = SDL_GetTicks() + 1000;
        while (teardown.low && SDL_GetTicks() < deadline) {
            SDL_UpdateGamepads();
            SDL_Delay(1);
        }
        check(teardown.low == 0, "Native duration did not expire without simulation ticks");
        scoped.rumble({1, 1, 5000});
    }
    check(teardown.low == 0, "Device owner teardown left feedback active");
    check(SDL_DetachVirtualJoystick(c), SDL_GetError());
}
void play(const char* runtime, const std::filesystem::path& root) {
    Feedback output;
    const auto pad = attach(&output);
    {
        Json project, scene;
        std::ifstream(root / "forge.project.json") >> project;
        std::ifstream(root / "feedback.scene.json") >> scene;
        PlaySession session;
        session.configure(60, InputMap(project.at("input")), {0, -9.81, 0}, root, true, true);
        session.set_user_data_override(root / "users");
        session.set_headless_audio(true);
        GameInput input;
        SdkPlatformEffects effects(session, input, {});
        bool focused = true;
        auto pump = [&] {
            session.pump();
            input.pump(session, true);
            effects.set_game_focused(focused);
            effects.pump(true);
            Json ack;
            effects.take_pending_ack(ack);
            SDL_UpdateGamepads();
            SDL_Delay(1);
        };
        auto wait = [&](auto done) {
            const auto deadline = SDL_GetTicks() + 10000;
            while (!done() && session.active() && SDL_GetTicks() < deadline)
                pump();
            if (!done())
                throw std::runtime_error("Feedback Play timeout: " + session.status() + " " +
                                         session.log());
        };
        auto probe = [&] {
            return session.snapshot()
                .at("entities")
                .at(0)
                .at("components")
                .at("project.feedback_probe");
        };
        session.start(runtime, scene);
        wait([&] { return !session.sdk_candidate_envelope().is_null(); });
        check(session.submit_editor_observation(false, "KeyboardMouse"), "Initial epoch rejected");
        check(session.submit_sdk_candidate_ack(true, session.session(),
                                               session.sdk_candidate_ticket()),
              "Candidate ack rejected");
        wait([&] { return session.control_ready(); });
        check(input.capture_checked(session, false), "Menu routing failed");
        SDL_Event select{};
        select.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
        select.gaxis.which = pad;
        select.gaxis.axis = SDL_GAMEPAD_AXIS_LEFTX;
        select.gaxis.value = 30000;
        input.event(select, session);
        auto key = [&](SDL_Scancode code) {
            SDL_Event event{};
            event.type = SDL_EVENT_KEY_DOWN;
            event.key.scancode = code;
            event.key.down = true;
            input.event(event, session);
            event.type = SDL_EVENT_KEY_UP;
            event.key.down = false;
            input.event(event, session);
        };
        auto pulse = [&](bool failure) {
            const auto completed = probe().at("completed").get<unsigned>();
            const auto failures = probe().at("failed").get<unsigned>();
            key(SDL_SCANCODE_R);
            wait([&] { return probe().at("completed").get<unsigned>() > completed; });
            check(probe().at("failed").get<unsigned>() == failures + unsigned(failure),
                  "Rumble receipt did not report actual platform success/failure");
        };
        pulse(false);
        check(output.low == 32768 && output.high == 49151,
              "Public module did not reach real SDL callback");
        const auto calls = output.calls;
        for (int i = 0; i != 12; ++i)
            pump();
        check(output.calls == calls, "Repeated platform offer replayed physical rumble");
        const auto completed = probe().at("completed").get<unsigned>();
        key(SDL_SCANCODE_T);
        wait([&] { return probe().at("completed").get<unsigned>() > completed; });
        check(output.low == 0 && output.high == 0, "Module Stop did not stop actual feedback");
        pulse(false);
        input.pump(session, false);
        check(output.low == 0, "Focus/routing loss left feedback active");
        input.pump(session, true);
        check(input.capture_checked(session, false), "Menu routing restore failed");
        focused = false;
        pulse(true);
        check(output.low == 0, "Unfocused editor applied feedback");
        focused = true;
        pulse(false);
        session.stop();
        effects.clear();
        check(output.low == 0, "Stop left controller feedback active");
    }
    check(SDL_DetachVirtualJoystick(pad), SDL_GetError());
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need runtime and prepared project");
        if (!SDL_Init(SDL_INIT_GAMEPAD))
            throw std::runtime_error(std::string("SDL gamepad initialization: ") + SDL_GetError());
        values_and_devices();
        play(argv[1], std::filesystem::absolute(argv[2]));
        SDL_Quit();
        std::cout
            << "Shared feedback validation, real SDL devices and public C++/Play adapter passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
