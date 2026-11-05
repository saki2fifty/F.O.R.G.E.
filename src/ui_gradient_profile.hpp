#pragma once
#include <array>
#include <cstdint>
#include <span>

namespace forge::ui_gradient_detail {
inline constexpr unsigned profile_version = 1;
inline constexpr unsigned max_stops = 16, max_shaders = 256;
struct Stop {
    float position = 0;
    // Premultiplied display-space channels, matching native UI geometry.
    std::array<float, 4> color{};
};
struct Gradient {
    // Pixel-local origin and projection vector; dot(uv - origin, projection).
    std::array<float, 4> line{};
    std::array<float, max_stops> positions{};
    std::array<std::array<float, 4>, max_stops> colors{};
    std::uint32_t count = 0;
    bool repeating = false;
};
// The native parser owns angle/length/unit/auto-stop resolution. This owner
// admits its resolved copied values without retaining native objects.
Gradient make_linear_gradient(std::array<float, 2> start, std::array<float, 2> end, double length,
                              bool repeating, std::span<const Stop> stops);
} // namespace forge::ui_gradient_detail
