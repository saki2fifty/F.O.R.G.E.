#include "editor/game_input.hpp"
#include "sdl_touches.hpp"
#include <forge/game_settings.hpp>
#include <iostream>
#include <limits>
#include <source_location>
using namespace forge;
namespace {
void check(bool value, const char* message,
           const std::source_location where = std::source_location::current()) {
    if (!value)
        throw std::runtime_error(std::to_string(where.line()) + ": " + message);
}
void near(double value, double expected) {
    check(std::abs(value - expected) < 1e-6, "Touch value differs");
}
template <class F> void rejects(F run) {
    try {
        run();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid touch candidate accepted");
}
struct Platform {
    Platform() {
#ifndef _WIN32
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
#endif
        const bool initialized = SDL_Init(SDL_INIT_VIDEO);
        check(initialized, SDL_GetError());
        window = SDL_CreateWindow("FORGE touch regression", 640, 400, SDL_WINDOW_HIDDEN);
        check(window, SDL_GetError());
    }
    ~Platform() {
        SDL_DestroyWindow(window);
        SDL_Quit();
    }
    SDL_Window* window = nullptr;
};
SDL_Event finger(SDL_Window* window, SDL_EventType type, Uint64 device, Uint64 contact,
                 float x = .5f, float y = .5f, float pressure = .6f) {
    SDL_Event event{};
    event.type = type;
    event.tfinger.windowID = SDL_GetWindowID(window);
    event.tfinger.touchID = device;
    event.tfinger.fingerID = contact;
    event.tfinger.x = x;
    event.tfinger.y = y;
    event.tfinger.pressure = pressure;
    return event;
}
Json mapping() {
    Json actions = Json::array();
    for (std::size_t slot = 0; slot < touch_contact_limit; ++slot)
        for (const auto* field : {"down", "x", "y", "pressure"})
            actions.push_back(
                {{"id", ActionId::generate()},
                 {"name", std::to_string(slot) + field},
                 {"kind", std::string_view(field) == "down" ? "digital" : "axis1"},
                 {"bindings",
                  Json::array({{{"control", "touch." + std::to_string(slot) + "." + field}}})}});
    return {{"version", 1}, {"actions", actions}};
}
void adapter(SDL_Window* window) {
    SdlTouches touches(window);
    RuntimeInput input;
    const auto source = mapping();
    input.configure(InputMap(source));
    auto queue = [&](SDL_Event value, bool accept = true,
                     std::optional<SdlTouches::Viewport> viewport = {}) {
        // Native SDL queue, not an invented second event representation.
        check(SDL_PushEvent(&value), SDL_GetError());
        SDL_Event event{};
        std::vector<InputEvent> result;
        while (SDL_PollEvent(&event))
            if (sdl_finger_event(event)) {
                auto values = touches.event(event, accept, viewport);
                input.submit(values);
                result.insert(result.end(), values.begin(), values.end());
            }
        return result;
    };
    check(queue(finger(window, SDL_EVENT_FINGER_MOTION, 1, 1)).empty(),
          "Motion joined without down");
    auto foreign = finger(window, SDL_EVENT_FINGER_DOWN, 1, 1);
    ++foreign.tfinger.windowID;
    check(queue(foreign).empty(), "Foreign window acquired a slot");
    check(queue(finger(window, SDL_EVENT_FINGER_DOWN, SDL_MOUSE_TOUCHID, 1)).empty(),
          "Mouse emulation duplicated touch");
    check(queue(finger(window, SDL_EVENT_FINGER_DOWN, SDL_PEN_TOUCHID, 1)).empty(),
          "Pen emulation duplicated touch");
    for (std::size_t slot = 0; slot < touch_contact_limit; ++slot) {
        const auto values = queue(finger(window, SDL_EVENT_FINGER_DOWN, slot + 1, 7));
        check(values.size() == 4 && values[0].control == "touch." + std::to_string(slot) + ".down",
              "Slot assignment or device/finger key failed");
    }
    check(queue(finger(window, SDL_EVENT_FINGER_DOWN, 100, 100)).empty(), "Capacity exceeded");
    input.latch(1);
    const auto released = queue(finger(window, SDL_EVENT_FINGER_CANCELED, 3, 7));
    check(released.size() == 4 && released[0].control == "touch.2.down" && released[0].value == 0,
          "Cancel compacted slots");
    check(queue(finger(window, SDL_EVENT_FINGER_MOTION, 100, 100)).empty(),
          "Overflow contact joined on motion");
    auto moved = queue(finger(window, SDL_EVENT_FINGER_MOTION, 8, 7, .75f, .25f, .5f));
    check(moved.size() == 4 && moved[0].control == "touch.7.down",
          "Remaining contact was renumbered");
    near(moved[1].value, .75);
    near(moved[2].value, .25);
    moved = queue(finger(window, SDL_EVENT_FINGER_DOWN, 101, 100));
    check(moved[0].control == "touch.2.down", "Freed slot was not reused");
    for (const auto bad : {std::numeric_limits<float>::quiet_NaN(), -1.f, 2.f})
        check(queue(finger(window, SDL_EVENT_FINGER_MOTION, 8, 7, bad)).empty(),
              "Invalid sample poisoned a contact");
    auto invalid_end =
        finger(window, SDL_EVENT_FINGER_UP, 8, 7, std::numeric_limits<float>::quiet_NaN());
    check(queue(invalid_end).size() == 4, "Invalid terminal coordinates prevented release");
    input.submit(touches.synchronize(false));
    const auto neutral = input.latch(2);
    for (const auto& [id, state] : neutral.actions)
        check(state.x == 0 && !state.held, "Ownership loss retained a value");
    check(queue(finger(window, SDL_EVENT_FINGER_MOTION, 1, 7)).empty(),
          "Recapture revived held contact");
    const SdlTouches::Viewport crop{100, 50, 400, 300};
    check(queue(finger(window, SDL_EVENT_FINGER_DOWN, 5, 1, .05f, .05f), true, crop).empty(),
          "Outside start admitted");
    check(queue(finger(window, SDL_EVENT_FINGER_MOTION, 5, 1), true, crop).empty(),
          "Outside start joined by drag");
    moved = queue(finger(window, SDL_EVENT_FINGER_DOWN, 6, 1), true, crop);
    near(moved[1].value, .55);
    near(moved[2].value, .5);
    moved = queue(finger(window, SDL_EVENT_FINGER_MOTION, 6, 1, 1, 0), true, crop);
    near(moved[1].value, 1);
    near(moved[2].value, 0);
    check(touches.synchronize(true, SdlTouches::Viewport{101, 50, 400, 300}).size() == 4,
          "Viewport change retained contact");
    check(queue(finger(window, SDL_EVENT_FINGER_DOWN, 1, 1)).size() == 4, "Fresh contact failed");
    check(SDL_SetWindowSize(window, 800, 500), SDL_GetError());
    check(touches.synchronize(true).size() == 4, "Window resize retained contact");
    check(queue(finger(window, SDL_EVENT_FINGER_MOTION, 1, 1)).empty(), "Resize revived contact");
    check(SDL_SetWindowSize(window, 640, 400), SDL_GetError());
    const auto first = input.map().actions().front().id;
    input.submit({{"touch.0.down", 1}, {"touch.0.down", 0}});
    auto snapshot = input.latch(3);
    check(snapshot.actions.at(first).pressed && snapshot.actions.at(first).released &&
              !snapshot.actions.at(first).held,
          "Quick tap lost fixed-tick edges");
    snapshot = input.latch(4);
    check(!snapshot.actions.at(first).pressed && !snapshot.actions.at(first).released,
          "Catch-up tick repeated edges");
    rejects([&] { input.submit({{"touch.0.down", 1}, {"touch.0.x", -.1}}); });
    check(!input.latch(5).actions.at(first).held, "Invalid batch partially committed");
    for (const auto& id : {"touch.8.down", "touch.-1.x", "touch.0.other"})
        rejects([&] { input_control(id); });
    auto invalid = source;
    invalid["actions"][1]["bindings"][0]["direction"] = -1;
    rejects([&] { InputMap bad(invalid); });
    input.begin_rebind();
    input.submit({{"touch.0.x", .9}, {"touch.0.pressure", 1}});
    check(input.rebinding(), "Touch movement stole rebind");
    input.submit({{"touch.0.down", 1}});
    const auto binding = input.take_rebind();
    check(binding && binding->control == "touch.0.down", "Contact down did not complete rebind");
    const auto resolved = resolve_game_settings(default_game_settings("org.forge.touch", "Touch"),
                                                Json::object(), InputMap(source));
    check(resolved.at("input_map") == source, "Gamepad settings altered touch bindings");
}
void editor(SDL_Window* window, const char* runtime) {
    const auto source = mapping();
    PlaySession play;
    play.configure(60, InputMap(source));
    play.start(runtime, {{"version", 1}, {"entities", Json::array()}}, true);
    const auto wait = [&](auto predicate) {
        const auto until = SDL_GetTicks() + 10000;
        while (!predicate() && play.active() && SDL_GetTicks() < until) {
            play.pump();
            SDL_Delay(1);
        }
        check(play.active() && predicate(), ("Touch Play: " + play.status() + play.log()).c_str());
    };
    wait([&] { return play.control_ready(); });
    GameInput input(window);
    input.viewport({100, 50}, {400, 300});
    input.pump(play, true);
    input.capture(play);
    const auto tick = [&] {
        wait([&] { return play.control_ready(); });
        const auto before = play.timing().at("tick").get<std::uint64_t>();
        play.step();
        wait([&] { return play.timing().at("tick").get<std::uint64_t>() > before; });
        return play.input_status().at("actions");
    };
    check(input.event(finger(window, SDL_EVENT_FINGER_DOWN, 1, 1), play),
          "Captured touch leaked to editor");
    auto values = tick();
    const auto value = [&](const Json& rows, unsigned index) {
        for (const auto& row : rows)
            if (row.at("id") == source.at("actions")[index].at("id"))
                return row;
        throw std::runtime_error("Touch action missing");
    };
    check(value(values, 0).at("held") == true, "Native touch did not reach actual Play");
    near(value(values, 1).at("x"), .55);
    near(value(values, 2).at("x"), .5);
    SDL_Event mouse{};
    mouse.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    mouse.button.which = SDL_TOUCH_MOUSEID;
    check(input.event(mouse, play) && input.captured(),
          "Synthetic touch mouse surrendered capture");
    SDL_Event lost{};
    lost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    input.event(lost, play);
    values = tick();
    check(!input.captured() && value(values, 0).at("held") == false &&
              value(values, 1).at("x") == 0,
          "Focus loss failed touch neutralization");
    input.capture(play);
    input.event(finger(window, SDL_EVENT_FINGER_MOTION, 1, 1), play);
    check(value(tick(), 0).at("held") == false, "Editor recapture revived old contact");
    play.stop();
}
} // namespace
int main(int argc, char** argv) {
    try {
        Platform platform;
        adapter(platform.window);
        if (argc > 1)
            editor(platform.window, argv[1]);
        std::cout << "SDL queued touch slots/bounds/cancel/neutralization and actual captured Play "
                     "passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
