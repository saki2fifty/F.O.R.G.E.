#pragma once
inline void single_gestures(SDL_Window* window) {
    check(SDL_SetWindowSize(window, 512, 512), SDL_GetError());
    SdlTouches touches(window);
    auto send = [&](SDL_EventType type, Uint64 time, float x = .5f, float y = .5f,
                    Uint64 contact = 1, Uint64 device = 17) {
        auto event = finger(window, type, device, contact, x, y);
        event.tfinger.timestamp = time * 1000000;
        return touches.event(event, true);
    };
    const auto pulses = [](const std::vector<InputEvent>& events) {
        std::vector<std::string> names;
        for (std::size_t i = 0; i < events.size(); ++i) {
            const auto& event = events[i];
            if (!event.control.starts_with("touch.gesture.") ||
                event.control == "touch.gesture.active" || event.value != 1)
                continue;
            check(i + 1 < events.size() && events[i + 1].control == event.control &&
                      events[i + 1].value == 0,
                  "Classification pulse did not release immediately");
            names.push_back(event.control.substr(14));
        }
        return names;
    };
    const auto expect = [&](const std::vector<InputEvent>& events,
                            std::vector<std::string> expected) {
        check(pulses(events) == expected, "Unexpected single-contact classification");
    };
    send(SDL_EVENT_FINGER_DOWN, 100);
    expect(send(SDL_EVENT_FINGER_UP, 350, .515625f), {"tap"}); // Exactly8px/250ms.
    send(SDL_EVENT_FINGER_DOWN, 500, .5625f);
    expect(send(SDL_EVENT_FINGER_UP, 700, .5625f), {"tap", "double_tap"}); // 24px/350ms.
    send(SDL_EVENT_FINGER_DOWN, 800);
    expect(send(SDL_EVENT_FINGER_UP, 850), {"tap"}); // Triple begins a new pair.
    send(SDL_EVENT_FINGER_DOWN, 900);
    expect(send(SDL_EVENT_FINGER_UP, 950, .5f, .5f, 1, 19), {}); // Unknown device UP.
    expect(send(SDL_EVENT_FINGER_UP, 960), {"tap", "double_tap"});
    send(SDL_EVENT_FINGER_DOWN, 1000, .5f, .5f, 1, 19);
    expect(send(SDL_EVENT_FINGER_UP, 1050, .5f, .5f, 1, 19), {"tap"});
    send(SDL_EVENT_FINGER_DOWN, 1100);
    expect(touches.advance(1599 * 1000000ULL), {});
    expect(touches.advance(1600 * 1000000ULL), {"long_press"});
    expect(touches.advance(1700 * 1000000ULL), {});
    expect(send(SDL_EVENT_FINGER_UP, 1750), {});
    send(SDL_EVENT_FINGER_DOWN, 2000);
    expect(send(SDL_EVENT_FINGER_UP, 2500), {"long_press"}); // No intermediate frame.
    for (unsigned direction = 0; direction < 4; ++direction) {
        const Uint64 time = 3000 + direction * 1000;
        send(SDL_EVENT_FINGER_DOWN, time);
        const float x = .5f + (direction == 0 ? -.125f : direction == 1 ? .125f : 0);
        const float y = .5f + (direction == 2 ? -.125f : direction == 3 ? .125f : 0);
        const char* names[]{"swipe_left", "swipe_right", "swipe_up", "swipe_down"};
        expect(send(SDL_EVENT_FINGER_UP, time + 750, x, y), {names[direction]});
    }
    send(SDL_EVENT_FINGER_DOWN, 8000);
    expect(send(SDL_EVENT_FINGER_UP, 8751, .75f), {});
    send(SDL_EVENT_FINGER_DOWN, 9000);
    send(SDL_EVENT_FINGER_MOTION, 9050, .52f);
    expect(send(SDL_EVENT_FINGER_UP, 9100), {}); // Out-and-back is not a tap.
    send(SDL_EVENT_FINGER_DOWN, 10000);
    send(SDL_EVENT_FINGER_DOWN, 10010, .75f, .5f, 2);
    send(SDL_EVENT_FINGER_UP, 10020, .75f, .5f, 2);
    expect(touches.advance(11000 * 1000000ULL), {});
    expect(send(SDL_EVENT_FINGER_UP, 11010), {}); // Remaining contact cannot rejoin.
    send(SDL_EVENT_FINGER_DOWN, 12000);
    expect(send(SDL_EVENT_FINGER_CANCELED, 12100), {});
    expect(touches.advance(13000 * 1000000ULL), {});
    send(SDL_EVENT_FINGER_DOWN, 14000);
    send(SDL_EVENT_FINGER_MOTION, 13999);
    expect(send(SDL_EVENT_FINGER_UP, 14100), {});
    send(SDL_EVENT_FINGER_DOWN, 15000);
    touches.advance(15400 * 1000000ULL);
    expect(touches.advance(15399 * 1000000ULL), {});
    expect(send(SDL_EVENT_FINGER_UP, 15600), {});
    send(SDL_EVENT_FINGER_DOWN, 16000);
    touches.synchronize(false);
    expect(send(SDL_EVENT_FINGER_UP, 16100), {});
    send(SDL_EVENT_FINGER_DOWN, 17000);
    auto invalid = finger(window, SDL_EVENT_FINGER_MOTION, 17, 1);
    invalid.tfinger.timestamp = 17100 * 1000000ULL;
    invalid.tfinger.x = std::numeric_limits<float>::quiet_NaN();
    touches.event(invalid, true);
    expect(send(SDL_EVENT_FINGER_UP, 17150), {});
    send(SDL_EVENT_FINGER_DOWN, 18000);
    touches.reset();
    expect(touches.advance(19000 * 1000000ULL), {});
    // Native queue preserves nonzero event timestamps. A delayed event drain
    // processes the early UP before the hold clock advances to a later frame.
    auto down = finger(window, SDL_EVENT_FINGER_DOWN, 17, 1);
    down.tfinger.timestamp = 20000 * 1000000ULL;
    auto up = down;
    up.type = SDL_EVENT_FINGER_UP;
    up.tfinger.timestamp = 20100 * 1000000ULL;
    check(SDL_PushEvent(&down) && SDL_PushEvent(&up), SDL_GetError());
    std::vector<InputEvent> observed;
    SDL_Event event;
    while (SDL_PollEvent(&event))
        if (sdl_finger_event(event)) {
            const auto values = touches.event(event, true);
            observed.insert(observed.end(), values.begin(), values.end());
        }
    expect(observed, {"tap"});
    expect(touches.advance(21000 * 1000000ULL), {});
    touches.reset();
    send(SDL_EVENT_FINGER_DOWN, 22000);
    expect(send(SDL_EVENT_FINGER_UP, 22251), {}); // Just beyond Tap's time limit.
    send(SDL_EVENT_FINGER_DOWN, 23000);
    expect(send(SDL_EVENT_FINGER_UP, 23100, .517578125f), {}); // 9px, beyond Tap slop.
    send(SDL_EVENT_FINGER_DOWN, 24000);
    expect(send(SDL_EVENT_FINGER_UP, 24100, .623046875f), {}); // 63px is not a swipe.
    send(SDL_EVENT_FINGER_DOWN, 25000);
    expect(send(SDL_EVENT_FINGER_UP, 25100, .625f, .625f), {"swipe_right"});
    send(SDL_EVENT_FINGER_DOWN, 26000);
    expect(send(SDL_EVENT_FINGER_UP, 26100), {"tap"});
    send(SDL_EVENT_FINGER_DOWN, 26451);
    expect(send(SDL_EVENT_FINGER_UP, 26452), {"tap"}); // 352ms between releases.
    send(SDL_EVENT_FINGER_DOWN, 26500, .548828125f);
    expect(send(SDL_EVENT_FINGER_UP, 26510, .548828125f), {"tap"}); // 25px apart.
    send(SDL_EVENT_FINGER_DOWN, 26600, .548828125f, .5f, 1, 19);
    expect(send(SDL_EVENT_FINGER_UP, 26610, .548828125f, .5f, 1, 19), {"tap"});
    send(SDL_EVENT_FINGER_DOWN, 26700, .548828125f, .5f, 1, 19);
    touches.synchronize(true, SdlTouches::Viewport{1, 0, 512, 512});
    expect(send(SDL_EVENT_FINGER_UP, 26710, .548828125f, .5f, 1, 19), {});
    send(SDL_EVENT_FINGER_DOWN, 26800, .548828125f, .5f, 1, 19);
    expect(send(SDL_EVENT_FINGER_UP, 26810, .548828125f, .5f, 1, 19), {"tap"});
    // Both latch domains retain the same quick pulse exactly once.
    RuntimeInput input;
    const auto fixed_id = ActionId::generate(), control_id = ActionId::generate();
    input.configure(InputMap(
        {{"version", 2},
         {"actions",
          Json::array({{{"id", fixed_id},
                        {"name", "Tap"},
                        {"kind", "digital"},
                        {"context", "game"},
                        {"bindings", Json::array({{{"control", "touch.gesture.tap"}}})}},
                       {{"id", control_id},
                        {"name", "Menu tap"},
                        {"kind", "digital"},
                        {"context", "menu"},
                        {"bindings", Json::array({{{"control", "touch.gesture.tap"}}})}}})},
         {"contexts",
          Json::array(
              {{{"name", "game"}, {"phase", "fixed"}, {"active", true}, {"consume", false}},
               {{"name", "menu"}, {"phase", "control"}, {"active", true}, {"consume", false}}})}}));
    input.submit(observed);
    const auto fixed = input.latch(1).actions.at(fixed_id);
    const auto control = input.latch_controls(1).actions.at(control_id);
    check(!fixed.held && fixed.pressed && fixed.released && !control.held && control.pressed &&
              control.released && !input.latch(2).actions.at(fixed_id).pressed &&
              !input.latch_controls(2).actions.at(control_id).pressed,
          "Single-touch pulse did not retain independent one-latch edges");
    check(SDL_SetWindowSize(window, 640, 400), SDL_GetError());
}
