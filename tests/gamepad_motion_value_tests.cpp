#include <forge/input.hpp>
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
    throw std::runtime_error("Expected motion admission to reject");
}
void copied_motion() {
    GamepadSensorSample sample{true, true, true, 200, 123456789, 42, {.25, -1.5, 9.80665}};
    GamepadMotion motion{
        true, true, 1, "host:connection", {{"host:connection", 1, true, {}, sample}}};
    const Json original = motion;
    GamepadTopology topology{
        true, 1, "host:connection", {{"host:connection", "Controller", 1, true, false}}};
    motion.validate(topology);
    ++topology.revision;
    rejects([&] { motion.validate(topology); });
    --topology.revision;
    topology.devices[0].slot = 2;
    rejects([&] { motion.validate(topology); });
    topology.devices[0].slot = 1;
    auto disconnected = motion;
    disconnected.devices[0].connected = false;
    disconnected.devices[0].gyroscope = {};
    disconnected.validate(topology);
    topology.devices[0].connected = false;
    rejects([&] { motion.validate(topology); });
    check(original.get<GamepadMotion>().devices[0].gyroscope.value == sample.value,
          "Raw SI data was clamped, normalized or changed by copying");
    for (const auto& field : {"timestamp_ns", "sensor_timestamp_ns"}) {
        auto maximum = original;
        maximum["devices"][0]["gyroscope"][field] = std::numeric_limits<std::uint64_t>::max();
        check(Json(maximum.get<GamepadMotion>()) == maximum,
              "Unsigned nanosecond time lost precision");
    }
    for (unsigned test = 0; test < 7; ++test) {
        auto invalid = original;
        auto& sensor = invalid["devices"][0]["gyroscope"];
        switch (test) {
        case 0:
            invalid["routed"] = false;
            break;
        case 1:
            sensor["enabled"] = false;
            break;
        case 2:
            sensor["measured"] = false;
            break;
        case 3:
            sensor["value"][1] = true;
            break;
        case 4:
            sensor["timestamp_ns"] = 1.5;
            break;
        case 5:
            sensor["sensor_timestamp_ns"] = -1;
            break;
        case 6:
            invalid["devices"].push_back(invalid["devices"][0]);
            break;
        }
        rejects([&] { from_json(invalid, motion); });
        check(Json(motion) == original, "Bad motion packet partially replaced the current view");
    }
    auto zero = motion;
    zero.devices[0].gyroscope.value = {};
    check(Json(zero).get<GamepadMotion>().devices[0].gyroscope.measured,
          "A measured zero was confused with an absent measurement");
    zero.devices[0].gyroscope.measured = false;
    zero.devices[0].gyroscope.timestamp_ns = 0;
    zero.devices[0].gyroscope.sensor_timestamp_ns = 0;
    zero.routed = false;
    check(!Json(zero).get<GamepadMotion>().devices[0].gyroscope.measured,
          "Supported/enabled but unrouted sensor invented a sample");
    for (auto bad :
         {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        auto value = motion;
        value.devices[0].gyroscope.value[0] = bad;
        rejects([&] { value.validate(); });
    }
    check(!Json(GamepadMotion{}).get<GamepadMotion>().available,
          "An absent native host appeared available");
}
void sensor_configuration() {
    GamepadSensorConfig config{1, "host:connection", GamepadSensorKind::Gyroscope, true};
    auto encoded = Json(config);
    encoded["operation"] = "gamepad_sensor";
    check(encoded.get<GamepadSensorConfig>().enabled, "Valid configuration did not copy");
    for (unsigned test = 0; test < 3; ++test) {
        auto invalid = encoded;
        switch (test) {
        case 0:
            invalid["enabled"] = 1;
            break;
        case 1:
            invalid["sensor"] = "orientation";
            break;
        case 2:
            invalid["revision"] = 0;
            break;
        }
        const auto before = Json(config);
        rejects([&] { from_json(invalid, config); });
        check(Json(config) == before, "Bad sensor configuration partially changed intent");
    }
}
} // namespace
int main() {
    try {
        copied_motion();
        sensor_configuration();
        std::cout << "Raw SI, precise timestamps, fresh routing and atomic motion/config admission "
                     "passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
