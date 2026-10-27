#pragma once
#include <numbers>
inline void gestures(SDL_Window* window) {
    SdlTouches touches(window);
    RuntimeInput input;
    Json actions = Json::array();
    std::array<std::array<ActionId, 5>, 2> ids;
    const char* fields[]{"active", "pan_x", "pan_y", "pinch", "rotation"};
    for (unsigned domain = 0; domain < 2; ++domain)
        for (unsigned field = 0; field < 5; ++field) {
            ids[domain][field] = ActionId::generate();
            actions.push_back(
                {{"id", ids[domain][field]},
                 {"name", fields[field]},
                 {"kind", field == 0 ? "digital" : "axis1"},
                 {"context", domain == 0 ? "game" : "ui"},
                 {"bindings",
                  Json::array({{{"control", std::string("touch.gesture.") + fields[field]}}})}});
        }
    const InputMap map(
        {{"version", 2},
         {"actions", actions},
         {"contexts",
          Json::array(
              {{{"name", "game"}, {"phase", "fixed"}, {"active", true}, {"consume", false}},
               {{"name", "ui"}, {"phase", "control"}, {"active", true}, {"consume", false}}})}});
    input.configure(map);
    auto send = [&](SDL_EventType type, Uint64 id, float x, float y) {
        const auto events = queued_touch(touches, finger(window, type, 17, id, x, y));
        input.submit(events);
        std::map<std::string, double> observed;
        for (const auto& e : events)
            if (e.control.starts_with("touch.gesture."))
                observed[e.control.substr(14)] = e.value;
        return observed;
    };
    send(SDL_EVENT_FINGER_DOWN, 1, .25f, .5f);
    auto values = send(SDL_EVENT_FINGER_DOWN, 2, .75f, .5f);
    check(values.size() == 1 && values.at("active") == 1, "Pair formation introduced a delta");
    input.latch(1);
    input.latch_controls(1);
    values = send(SDL_EVENT_FINGER_MOTION, 2, 1, .5f);
    near(values.at("pan_x"), .125);
    near(values.at("pinch"), std::log(1.5));
    check(!values.contains("rotation") && !values.contains("pan_y"),
          "Horizontal pinch invented rotation/panY");
    auto fixed = input.latch(2);
    near(fixed.actions.at(ids[0][1]).x, .125);
    near(fixed.actions.at(ids[0][3]).x, std::log(1.5));
    near(input.latch(3).actions.at(ids[0][3]).x, 0);
    auto control = input.latch_controls(2);
    near(control.actions.at(ids[1][1]).x, .125);
    near(control.actions.at(ids[1][3]).x, std::log(1.5));
    near(input.latch_controls(3).actions.at(ids[1][3]).x, 0);
    values = send(SDL_EVENT_FINGER_MOTION, 1, .25f, .9f);
    near(values.at("pan_y"), .2);
    near(values.at("pinch"), .5 * std::log(10. / 9.));
    near(values.at("rotation"), std::atan2(-160., 480.));
    check(send(SDL_EVENT_FINGER_DOWN, 3, .8f, .8f).empty(), "Third finger stole the pair");
    check(send(SDL_EVENT_FINGER_MOTION, 3, .1f, .1f).empty(),
          "Third finger movement emitted a gesture");
    check(send(SDL_EVENT_FINGER_UP, 1, 0, 0).empty(),
          "Replacement pair produced a jump/active edge");
    values = send(SDL_EVENT_FINGER_UP, 3, 0, 0);
    check(values.size() == 1 && values.at("active") == 0, "Pair end retained active gesture");
    send(SDL_EVENT_FINGER_UP, 2, 0, 0);
    send(SDL_EVENT_FINGER_DOWN, 1, .5f, .5f);
    send(SDL_EVENT_FINGER_DOWN, 2, .25f, .51f);
    values = send(SDL_EVENT_FINGER_MOTION, 2, .25f, .49f);
    near(values.at("rotation"), 2 * std::atan2(4., 160.));
    check(values.at("rotation") > 0 && values.at("rotation") < .1,
          "Angle branch crossing caused full turn");
    input.submit(touches.synchronize(false));
    check(send(SDL_EVENT_FINGER_MOTION, 1, .5f, .5f).empty(), "Capture loss revived gesture");
    send(SDL_EVENT_FINGER_DOWN, 1, .5f, .5f);
    send(SDL_EVENT_FINGER_DOWN, 2, .5f, .5f);
    values = send(SDL_EVENT_FINGER_MOTION, 2, .501f, .5f);
    check(!values.contains("pinch") && !values.contains("rotation"),
          "Subpixel pair emitted unstable scale/angle");
    values = send(SDL_EVENT_FINGER_MOTION, 2, .75f, .5f);
    check(!values.contains("pinch") && !values.contains("rotation"),
          "Separation from coincidence did not rebase");
    values = send(SDL_EVENT_FINGER_MOTION, 2, .875f, .5f);
    near(values.at("pinch"), std::log(1.5));
    touches.reset();
    input.configure(map);
    send(SDL_EVENT_FINGER_DOWN, 1, .25f, .5f);
    send(SDL_EVENT_FINGER_DOWN, 2, .75f, .5f);
    send(SDL_EVENT_FINGER_MOTION, 2, 1, .5f);
    send(SDL_EVENT_FINGER_CANCELED, 2, 0, 0);
    send(SDL_EVENT_FINGER_UP, 1, 0, 0);
    fixed = input.latch(1);
    const auto active = fixed.actions.at(ids[0][0]);
    check(!active.held && active.pressed && active.released, "Quick gesture lost active edges");
    near(fixed.actions.at(ids[0][1]).x, .125);
    near(fixed.actions.at(ids[0][3]).x, std::log(1.5));
    near(input.latch(2).actions.at(ids[0][3]).x, 0);
    control = input.latch_controls(1);
    near(control.actions.at(ids[1][3]).x, std::log(1.5));
    send(SDL_EVENT_FINGER_DOWN, 1, .25f, .5f);
    send(SDL_EVENT_FINGER_DOWN, 2, .75f, .5f);
    const auto cleared = touches.synchronize(true, SdlTouches::Viewport{10, 10, 500, 300});
    check(std::any_of(
              cleared.begin(), cleared.end(),
              [](const auto& e) { return e.control == "touch.gesture.active" && e.value == 0; }),
          "Viewport change retained gesture");
    check(queued_touch(touches, finger(window, SDL_EVENT_FINGER_MOTION, 17, 2)).empty(),
          "Viewport change joined old finger");
}
