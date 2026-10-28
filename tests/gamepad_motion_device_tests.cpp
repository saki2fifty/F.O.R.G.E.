#include "gamepad_motion_fixture.hpp"
#include "sdl_gamepads.hpp"
#include <iostream>
#include <limits>
using namespace forge;
using Json = nlohmann::json;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F run) {
    try {
        run();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Expected sensor operation to reject");
}
void owner() {
    test::GamepadMotionDeviceFixture first, second;
    {
        SdlGamepads pads;
        pads.discover();
        auto topology = pads.snapshot();
        const auto a = topology.devices.at(0).device, b = topology.devices.at(1).device;
        auto configure = [&](const std::string& device, GamepadSensorKind kind, bool enabled) {
            pads.configure_sensor({pads.snapshot().revision, device, kind, enabled});
        };
        configure(a, GamepadSensorKind::Gyroscope, true);
        configure(b, GamepadSensorKind::Accelerometer, true);
        rejects([&] {
            pads.configure_sensor({topology.revision, a, GamepadSensorKind::Gyroscope, false});
        });
        check(!pads.motion(true).routed && !pads.motion(true).devices[0].gyroscope.measured,
              "Sensor configuration invented input routing or a measurement");
        pads.route_motion(true);
        auto drain = [&](bool routed = true) {
            SDL_UpdateJoysticks();
            SDL_Event event;
            while (SDL_PollEvent(&event))
                pads.event(event, routed, SDL_GetTicks());
        };
        drain();
        first.send(SDL_SENSOR_GYRO, {.25f, -.5f, 1.5f}, 123456789);
        second.send(SDL_SENSOR_ACCEL, {0, SDL_STANDARD_GRAVITY, 0}, 234567890);
        drain();
        auto before = pads.motion(true);
        check(before.routed && before.devices[0].gyroscope.measured &&
                  before.devices[0].gyroscope.value[2] == 1.5 &&
                  before.devices[0].gyroscope.sensor_timestamp_ns == 123456789 &&
                  before.devices[1].accelerometer.measured,
              "Two real virtual sensors lost SI values, timestamps or independent measurements");
        check(Json(pads.motion(true)) == Json(before),
              "Reading motion changed its sample or clock");
        check(!pads.motion(false).devices[0].gyroscope.measured &&
                  pads.motion(true).devices[0].gyroscope.measured,
              "Masked projection leaked data or changed physical routing");
        first.fail = true;
        const auto revision = pads.snapshot().revision;
        rejects([&] { configure(a, GamepadSensorKind::Gyroscope, false); });
        check(first.enabled && pads.snapshot().revision == revision &&
                  Json(pads.motion(true)) == Json(before),
              "Failed disable changed mode, revision or measured input");
        first.fail = false;
        pads.route_motion(false);
        pads.route_motion(true);
        SDL_Event old{};
        old.type = SDL_EVENT_GAMEPAD_SENSOR_UPDATE;
        old.gsensor.which = first.id;
        old.gsensor.sensor = SDL_SENSOR_GYRO;
        old.gsensor.timestamp = before.devices[0].gyroscope.timestamp_ns;
        old.gsensor.data[0] = 100;
        pads.event(old, true, SDL_GetTicks());
        check(!pads.motion(true).devices[0].gyroscope.measured,
              "Pre-capture queued sample reappeared after routing resumed");
        first.send(SDL_SENSOR_GYRO, {0, 0, 2}, 345678901);
        second.send(SDL_SENSOR_ACCEL, {1, 2, 3}, 456789012);
        drain();
        check(pads.motion(true).devices[0].gyroscope.measured,
              "Fresh sample did not resume after routing boundary");
        SDL_Event invalid = old;
        invalid.gsensor.timestamp = SDL_GetTicksNS();
        invalid.gsensor.data[0] = std::numeric_limits<float>::quiet_NaN();
        pads.event(invalid, true, SDL_GetTicks());
        check(!pads.motion(true).devices[0].gyroscope.measured &&
                  pads.motion(true).devices[1].accelerometer.measured,
              "Bad native data retained a stale sample or changed another controller");
        first.send(SDL_SENSOR_GYRO, {0, 0, 3}, 567890123);
        drain();
        SDL_Event remap{};
        remap.type = SDL_EVENT_GAMEPAD_REMAPPED;
        remap.gdevice.which = first.id;
        pads.event(remap, true, SDL_GetTicks());
        check(!pads.motion(true).devices[0].gyroscope.measured &&
                  pads.motion(true).devices[1].accelerometer.measured,
              "Remapping retained the target sample or cleared another controller");
        first.send(SDL_SENSOR_GYRO, {0, 1, 0}, 678901234);
        second.send(SDL_SENSOR_ACCEL, {3, 2, 1}, 789012345);
        drain();
        pads.assign({pads.snapshot().revision, a, 2});
        check(!pads.motion(true).devices[0].gyroscope.measured &&
                  !pads.motion(true).devices[1].accelerometer.measured,
              "Player reassignment retained either affected controller's motion");
    }
    check(!first.enabled && !second.enabled,
          "Device owner teardown did not request native sensor disable");
}
void asynchronous_disconnect() {
    test::GamepadMotionDeviceFixture first, second;
    SdlGamepads pads;
    pads.discover();
    for (std::size_t index = 0; index < 2; ++index) {
        const auto topology = pads.snapshot();
        pads.configure_sensor({topology.revision, topology.devices[index].device,
                               GamepadSensorKind::Gyroscope, true});
    }
    pads.route_motion(true);
    auto drain = [&] {
        SDL_UpdateJoysticks();
        SDL_Event event;
        while (SDL_PollEvent(&event))
            pads.event(event, true, SDL_GetTicks());
    };
    first.send(SDL_SENSOR_GYRO, {1, 2, 3}, 12);
    second.send(SDL_SENSOR_GYRO, {3, 2, 1}, 34);
    drain();
    const auto topology = pads.snapshot();
    const auto gone = topology.devices[1].device;
    check(SDL_DetachVirtualJoystick(second.id), SDL_GetError());
    second.id = 0;
    const auto motion = pads.motion(true);
    check(!motion.devices[1].connected && !motion.devices[1].gyroscope.measured &&
              motion.devices[0].gyroscope.measured,
          "Native disconnect retained raw data or changed another controller");
    motion.validate(topology); // Disconnect happened between copied observations.
    drain();
    check(pads.snapshot().devices.size() == 1, "Removal event retained the disconnected sensor");
    rejects([&] {
        pads.configure_sensor({pads.snapshot().revision, gone, GamepadSensorKind::Gyroscope, true});
    });
}
} // namespace
int main() {
    try {
        check(SDL_Init(SDL_INIT_GAMEPAD), SDL_GetError());
        struct Cleanup {
            ~Cleanup() { SDL_Quit(); }
        } cleanup;
        owner();
        asynchronous_disconnect();
        std::cout << "Native motion ownership, SI/fresh boundaries, failure retention, scoped data "
                     "and teardown passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
