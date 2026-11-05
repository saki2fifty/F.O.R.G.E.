#pragma once
#include <array>
#include <cstdint>
#include <span>

namespace forge::ui_gradient_detail {
inline constexpr unsigned basic_profile_version = 1;
inline constexpr unsigned profile_version = 2;
inline constexpr unsigned max_stops = 16, max_shaders = 256;
struct Stop {
    float position = 0;
    // Premultiplied display-space channels, matching native UI geometry.
    std::array<float, 4> color{};
};
enum class Kind : std::uint32_t { Linear = 1, Radial = 2, Conic = 3 };
struct Gradient {
    Kind kind = Kind::Linear;
    // Linear: origin/projection. Radial: center/inverse radii.
    // Conic: center/start angle in turns/unused.
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
Gradient make_radial_gradient(std::array<float, 2> center, std::array<float, 2> radius,
                              bool repeating, std::span<const Stop> stops);
Gradient make_conic_gradient(std::array<float, 2> center, double angle, bool repeating,
                             std::span<const Stop> stops);
} // namespace forge::ui_gradient_detail
