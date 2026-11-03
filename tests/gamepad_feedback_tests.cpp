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
void multiple_devices() {
    std::array<Feedback, gamepad_slot_limit> feedback;
    std::array<SDL_JoystickID, gamepad_slot_limit> ids;
    for (std::size_t i = 0; i < ids.size(); ++i)
        ids[i] = attach(&feedback[i]);
    {
        SdlGamepads pads;
        pads.discover();
        auto topology = pads.snapshot();
        check(topology.available && topology.devices.size() == ids.size() &&
                  topology.revision == ids.size(),
              "Device discovery did not publish all live connections");
        for (std::size_t i = 0; i < ids.size(); ++i)
            check(topology.devices[i].slot == i + 1,
                  "Default logical slots are not deterministic/bounded");
        check(Json(topology).get<GamepadTopology>().devices.size() == ids.size(),
              "Copied device observation did not round trip");
        const auto retained = Json(topology);
        for (auto invalid : {Json{{"available", true}}, retained}) {
            if (invalid == retained)
                invalid["devices"][1]["slot"] = 1;
            rejects([&] { from_json(invalid, topology); });
            check(Json(topology) == retained, "Invalid device observation partially applied");
        }
        for (const Json& bad : {Json(true), Json(-1), Json(17), Json(1.5)}) {
            auto request =
                Json(GamepadAssignment{topology.revision, topology.devices[0].device, 1});
            request["slot"] = bad;
            rejects([&] { (void)request.get<GamepadAssignment>(); });
            auto output = Json(GamepadRumble{.5, 0, 100});
            output["slot"] = bad;
            rejects([&] { (void)output.get<GamepadRumble>(); });
        }
        std::array<ActionId, gamepad_slot_limit> players, menus;
        const auto keyboard = ActionId::generate(), legacy = ActionId::generate();
        Json map{
            {"version", 2},
            {"contexts",
             Json::array(
                 {{{"name", "Game"}, {"active", true}, {"consume", false}},
                  {{"name", "Menu"}, {"active", true}, {"consume", false}, {"phase", "control"}}})},
            {"actions", Json::array()}};
        auto action = [&](ActionId id, const std::string& control, const std::string& context) {
            map["actions"].push_back({{"id", id},
                                      {"name", id.str()},
                                      {"kind", "digital"},
                                      {"context", context},
                                      {"bindings", Json::array({{{"control", control}}})}});
        };
        for (std::size_t i = 0; i < players.size(); ++i) {
            players[i] = ActionId::generate();
            menus[i] = ActionId::generate();
            action(players[i], "pad." + std::to_string(i + 1) + ".south", "Game");
            action(menus[i], "pad." + std::to_string(i + 1) + ".south", "Menu");
        }
        action(keyboard, "key.space", "Game");
        action(legacy, "pad.south", "Game");
        RuntimeInput input;
        input.configure(InputMap(map));
        input.submit({{"key.space", 1}});
        auto press = [&](std::size_t device, Uint64 now) {
            SDL_Event event{};
            event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
            event.gbutton.which = ids[device];
            event.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
            event.gbutton.down = true;
            const auto routed = pads.event(event, true, now);
            check(std::none_of(routed.begin(), routed.end(), [](const auto& e) { return e.reset; }),
                  "Controller routing used a global input reset");
            input.submit(routed);
            return routed;
        };
        press(0, 1000);
        press(1, 1000); // Legacy switches; player 1 remains held.
        check(press(2, 1100).size() == 1 && pads.active() == ids[1],
              "Legacy switch guard blocked independent player input");
        press(3, 1100);
        for (std::size_t i = 4; i < ids.size(); ++i)
            press(i, 1100);
        for (const auto* snapshot : {&input.latch(1), &input.latch_controls(1)})
            for (const auto id : snapshot == &input.snapshot() ? players : menus)
                check(snapshot->actions.at(id).held && snapshot->actions.at(id).pressed,
                      "Simultaneous players lost held/press edges in a latch domain");
        check(input.snapshot().actions.at(keyboard).held, "Controller switch released keyboard");
        for (std::uint32_t slot = 1; slot <= gamepad_slot_limit; ++slot)
            pads.rumble({.25, .5, 5000, slot});
        press(2, 1500);
        check(feedback[0].low != 0 && feedback[1].low != 0 && feedback[3].low != 0,
              "Active alias change stopped another player's indexed feedback");
        topology = pads.snapshot();
        const GamepadAssignment edit{topology.revision, topology.devices[9].device, 16};
        feedback[9].fail = true;
        rejects([&] { pads.assign(edit); });
        check(Json(pads.snapshot()) == Json(topology), "Failed native Stop changed assignments");
        feedback[9].fail = false;
        const auto neutral = pads.assign(edit);
        check(neutral.size() == 42, "Assignment did not neutralize both affected slots");
        input.submit(neutral);
        const auto fixed = input.latch(2), control = input.latch_controls(2);
        for (std::size_t i = 0; i < players.size(); ++i) {
            const bool unaffected = i != 9 && i != 15;
            check(fixed.actions.at(players[i]).held == unaffected &&
                      control.actions.at(menus[i]).held == unaffected,
                  "Assignment neutralized the wrong player or latch domain");
            if (!unaffected)
                check(fixed.actions.at(players[i]).released &&
                          control.actions.at(menus[i]).released,
                      "Assignment did not produce release edges");
        }
        check(fixed.actions.at(keyboard).held && feedback[9].low == 0 && feedback[15].low == 0 &&
                  feedback[0].low != 0 && feedback[1].low != 0 && feedback[3].low != 0,
              "Assignment changed unrelated input or feedback");
        rejects([&] { pads.assign(edit); }); // Revision is now stale.
        SdlGamepads other;
        other.discover();
        rejects([&] { other.assign({other.snapshot().revision, edit.device, 1}); });
        topology = pads.snapshot();
        check(topology.devices[9].slot == 16 && topology.devices[15].slot == 0,
              "Occupied logical slot was not transferred");
        SDL_Event removal{};
        removal.type = SDL_EVENT_GAMEPAD_REMOVED;
        removal.gdevice.which = ids[2];
        check(SDL_DetachVirtualJoystick(ids[2]), SDL_GetError());
        input.submit(pads.event(removal, false, 1600));
        const auto after = input.latch(3);
        check(after.actions.at(players[2]).released && !after.actions.at(players[2]).held &&
                  after.actions.at(players[3]).held && after.actions.at(keyboard).held,
              "Disconnect left a held action or reset an unrelated player");
        SDL_Event remap{};
        remap.type = SDL_EVENT_GAMEPAD_REMAPPED;
        remap.gdevice.which = ids[3];
        input.submit(pads.event(remap, false, 1700));
        const auto remapped = input.latch(4);
        check(remapped.actions.at(players[3]).released && remapped.actions.at(keyboard).held &&
                  feedback[3].low == 0,
              "Remapping retained old controls/feedback or reset the keyboard");
        pads.rumble({1, 0, 5000, 4});
        pads.rumble({});
        check(feedback[3].low == 0, "Untargeted Stop did not stop all owned slot feedback");
    }
    for (std::size_t i = 0; i < ids.size(); ++i)
        if (i != 2)
            check(SDL_DetachVirtualJoystick(ids[i]), SDL_GetError());
    // Bound native handle ownership even when all logical routes are occupied.
    std::array<Feedback, gamepad_device_limit + 1> capped_feedback;
    std::vector<SDL_JoystickID> capped;
    for (auto& feedback : capped_feedback)
        capped.push_back(attach(&feedback));
    {
        SdlGamepads pads;
        pads.discover();
        check(pads.count() == gamepad_device_limit && pads.snapshot().devices.size() == 16,
              "Native device ownership exceeded its documented bound");
    }
    for (auto id : capped)
        check(SDL_DetachVirtualJoystick(id), SDL_GetError());
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
        const auto topology = input.gamepads();
        wait([&] { return session.input_queue_room(4096); });
        for (unsigned queued = 0; queued < 4096; ++queued)
            session.input_event({"key.a", 0});
        rejects([&] {
            input.assign_gamepad(session, {topology.revision, topology.devices.at(0).device, 2});
        });
        check(Json(input.gamepads()) == Json(topology),
              "Full input queue allowed a partial physical assignment");
        wait([&] { return session.input_queue_room(4096); });
        const auto assigned = probe().at("completed").get<unsigned>();
        const auto assignment_failures = probe().at("failed").get<unsigned>();
        key(SDL_SCANCODE_Y);
        wait([&] { return probe().at("completed").get<unsigned>() > assigned; });
        check(probe().at("failed").get<unsigned>() == assignment_failures &&
                  input.gamepads().devices.at(0).slot == 2,
              "Public C++ Game query/request did not apply/acknowledge physical slot assignment");
        const auto player_pulse = probe().at("completed").get<unsigned>();
        key(SDL_SCANCODE_U);
        wait([&] { return probe().at("completed").get<unsigned>() > player_pulse; });
        check(output.low == 32768 && output.high == 49151,
              "Public C++ indexed feedback did not reach the reassigned real SDL device");
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
        multiple_devices();
        play(argv[1], std::filesystem::absolute(argv[2]));
        SDL_Quit();
        std::cout
            << "Shared feedback validation, real SDL devices and public C++/Play adapter passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
