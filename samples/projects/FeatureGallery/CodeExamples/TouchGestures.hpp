#pragma once
// Original Gallery89 ActionIds. Gesture deltas are already assigned to this tick.
#include <array>
#include <forge/sdk_client.hpp>
namespace forge::examples {
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
