#pragma once
// Original Gallery89 ActionIds. Gesture deltas are already assigned to this tick.
#include <array>
#include <forge/sdk_client.hpp>
namespace forge::examples {
inline constexpr const char* single_touch_scene = "16000000-1600-4160-8160-160000000000";
inline constexpr const char* single_touch_marker = "16000000-1600-4160-8160-000000000002";
inline constexpr std::array<const char*, 7> single_touch_actions{
    "16000000-1600-4160-8160-000000000010", "16000000-1600-4160-8160-000000000011",
    "16000000-1600-4160-8160-000000000012", "16000000-1600-4160-8160-000000000013",
    "16000000-1600-4160-8160-000000000014", "16000000-1600-4160-8160-000000000015",
    "16000000-1600-4160-8160-000000000016"};
inline constexpr const char* gesture_scene = "15600000-1560-4156-8156-156000000000";
inline constexpr const char* gesture_marker = "15600000-1560-4156-8156-000000000002";
inline constexpr std::array<const char*, 5> gesture_actions{
    "15600000-1560-4156-8156-000000000010", "15600000-1560-4156-8156-000000000011",
    "15600000-1560-4156-8156-000000000012", "15600000-1560-4156-8156-000000000013",
    "15600000-1560-4156-8156-000000000014"};
struct TouchGestureSample {
    bool active = false;
    double pan_x = 0, pan_y = 0, pinch = 0, rotation = 0;
};
inline bool read_touch_gesture(const sdk::Client& client, TouchGestureSample& out) {
    std::array<ForgeSdkActionV1, 5> values{};
    for (unsigned i = 0; i < values.size(); ++i)
        if (!client.action(gesture_actions[i], values[i])) {
            out = {};
            return false;
        }
    out = {values[0].held != 0, values[1].x, values[2].x, values[3].x, values[4].x};
    return true;
}
} // namespace forge::examples
