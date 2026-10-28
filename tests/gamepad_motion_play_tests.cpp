#include "editor/sdk_platform_effects.hpp"
#include "gamepad_motion_fixture.hpp"
#include <fstream>
#include <iostream>
using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void play(const char* runtime, const std::filesystem::path& root) {
    test::GamepadMotionDeviceFixture device;
    Json project, scene;
    std::ifstream(root / "forge.project.json") >> project;
    std::ifstream(root / "Scenes/93-gamepad-motion.scene.json") >> scene;
    PlaySession session;
    session.configure(60, InputMap(project.at("input")), {0, -9.81, 0}, root, true, true);
    session.set_user_data_override(root / "users");
    session.set_headless_audio(true);
    GameInput input;
    SdkPlatformEffects effects(session, input, {});
    bool allowed = true, acknowledged = false;
    auto pump = [&] {
        session.pump();
        input.pump(session, allowed);
        SDL_Event event;
        while (SDL_PollEvent(&event))
            input.event(event, session);
        input.finish_events(session);
        effects.set_game_focused(allowed);
        effects.pump(allowed);
        Json ack;
        effects.take_pending_ack(ack);
        if (!acknowledged && !session.sdk_candidate_envelope().is_null()) {
            check(session.submit_editor_observation(false, "KeyboardMouse"),
                  "Initial motion epoch failed");
            check(session.submit_sdk_candidate_ack(true, session.session(),
                                                   session.sdk_candidate_ticket()),
                  "Motion candidate acknowledgement failed");
            acknowledged = true;
        }
        SDL_Delay(1);
    };
    auto wait = [&](auto ready) {
        const auto deadline = SDL_GetTicks() + 10000;
        while (!ready() && session.active() && SDL_GetTicks() < deadline)
            pump();
        if (!ready())
            throw std::runtime_error("Motion Play timeout: " + session.status() + " " +
                                     session.log());
    };
    auto marker = [&](const char* id) {
        for (const auto& row : session.snapshot().at("entities"))
            if (row.at("id") == id)
                return row.at("components").at("forge.local_translation");
        throw std::runtime_error("Missing raw motion marker");
    };
    auto gyro_y = [&] {
        return marker("16800000-1680-4168-8168-000000000003").at("y").get<double>();
    };
    session.start(runtime, scene);
    wait([&] {
        return session.control_ready() && input.gamepad_motion(session).devices.size() == 1 &&
               input.gamepad_motion(session).devices[0].accelerometer.enabled &&
               input.gamepad_motion(session).devices[0].gyroscope.enabled;
    });
    session.pause();
    wait([&] { return session.control_ready() && session.paused(); });
    check(input.capture_checked(session, false), "Motion capture failed");
    pump(); // Consume the capture reset before subsequent native samples.
    device.send(SDL_SENSOR_ACCEL, {2, 9.80665f, -1}, 111111111);
    device.send(SDL_SENSOR_GYRO, {-1.5f, 2.5f, .25f}, 222222222);
    wait([&] { return input.gamepad_motion(session).devices[0].gyroscope.measured; });
    session.step();
    wait([&] { return session.control_ready() && gyro_y() == 2.5; });
    const auto tick = session.timing().at("tick");
    const auto raw = Json(input.gamepad_motion(session));
    check(raw.at("devices")[0].at("gyroscope").at("sensor_timestamp_ns") == 222222222,
          "Native nanosecond timestamp was changed by the editor");
    for (int i = 0; i < 5; ++i)
        pump();
    check(session.timing().at("tick") == tick && gyro_y() == 2.5,
          "Passive motion observation advanced paused simulation");
    allowed = false;
    pump();
    check(!input.gamepad_motion(session).routed &&
              !input.gamepad_motion(session).devices[0].gyroscope.measured,
          "Focus/modal release retained measured motion");
    allowed = true;
    check(input.capture_checked(session, false), "Motion recapture failed");
    session.step();
    wait([&] { return session.control_ready() && gyro_y() == 0; });
    device.send(SDL_SENSOR_GYRO, {0, 1.25f, 0}, 333333333);
    wait([&] { return input.gamepad_motion(session).devices[0].gyroscope.measured; });
    // Sensor traffic remains continuous while Pause/Step needs scheduler time.
    session.step();
    wait([&] {
        device.send(SDL_SENSOR_GYRO, {0, 1.25f, 0}, 444444444);
        return session.control_ready() && gyro_y() == 1.25;
    });
    input.release(session);
    session.stop();
    input.pump(session, true);
    effects.clear();
    check(!input.gamepad_motion(session).routed &&
              !input.gamepad_motion(session).devices[0].gyroscope.measured,
          "Stop retained a playable motion sample");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need runtime and prepared motion project");
        check(SDL_Init(SDL_INIT_GAMEPAD), SDL_GetError());
        struct Cleanup {
            ~Cleanup() { SDL_Quit(); }
        } cleanup;
        play(argv[1], std::filesystem::absolute(argv[2]));
        std::cout << "Actual SDL/GameInput/SDK sensors, SI markers, focus/recapture/Stop and clock "
                     "scheduling passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
