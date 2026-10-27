#pragma once
// Original Gallery88 example. These ActionIds match its saved project Input map.
#include <array>
#include <forge/sdk_client.hpp>
namespace forge::examples {
inline constexpr const char* touch_scene = "15500000-1550-4155-8155-155000000000";
inline constexpr const char* touch_marker = "15500000-1550-4155-8155-000000000002";
inline constexpr std::array<const char*, 4> touch_actions{
    "15500000-1550-4155-8155-000000000010", "15500000-1550-4155-8155-000000000011",
    "15500000-1550-4155-8155-000000000012", "15500000-1550-4155-8155-000000000013"};
struct TouchSample {
    bool down = false;
    double x = 0, y = 0, pressure = 0;
};
inline bool read_touch(const sdk::Client& client, TouchSample& out) {
    std::array<ForgeSdkActionV1, 4> values{};
    for (unsigned i = 0; i < values.size(); ++i)
        if (!client.action(touch_actions[i], values[i])) {
            out = {};
            return false;
        }
    out = {values[0].held != 0, values[1].x, values[2].x, values[3].x};
    return true;
}
} // namespace forge::examples
