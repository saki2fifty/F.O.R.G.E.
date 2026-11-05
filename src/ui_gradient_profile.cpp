#include "ui_gradient_profile.hpp"
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace forge::ui_gradient_detail {
namespace {
void validate_point(std::array<float, 2> point) {
    for (const auto value : point)
        if (!std::isfinite(value) || std::abs(value) > 65536)
            throw std::runtime_error(
                "UI gradient points require finite coordinates within -65536..65536 pixels");
}
Gradient copy_stops(Kind kind, std::array<float, 4> shape, bool repeating,
                    std::span<const Stop> stops) {
    if (stops.size() < 2 || stops.size() > max_stops)
        throw std::runtime_error("UI gradients require 2..16 resolved color stops");
    Gradient result;
    result.kind = kind;
    result.line = shape;
    result.count = std::uint32_t(stops.size());
    result.repeating = repeating;
    for (std::size_t i = 0; i < stops.size(); ++i) {
        const auto& stop = stops[i];
        if (!std::isfinite(stop.position) || std::abs(stop.position) > 65536 ||
            (i && stop.position < stops[i - 1].position))
            throw std::runtime_error(
                "UI gradient stops require finite ordered positions within -65536..65536");
        for (unsigned c = 0; c < 4; ++c)
            if (!std::isfinite(stop.color[c]) || stop.color[c] < 0 || stop.color[c] > 1 ||
                (c < 3 && stop.color[c] > stop.color[3]))
                throw std::runtime_error(
                    "UI gradient colors require finite premultiplied RGBA within 0..1");
        result.positions[i] = stop.position;
        result.colors[i] = stop.color;
    }
    if (repeating && double(stops.back().position) - stops.front().position < 1e-6)
        throw std::runtime_error("Repeating UI gradients require a nonzero resolved stop period");
    return result;
}
} // namespace
Gradient make_linear_gradient(std::array<float, 2> start, std::array<float, 2> end, double length,
                              bool repeating, std::span<const Stop> stops) {
    validate_point(start);
    validate_point(end);
    const double dx = double(end[0]) - start[0], dy = double(end[1]) - start[1];
    const double distance = std::hypot(dx, dy);
    if (!std::isfinite(length) || length < 1e-4 || distance < 1e-4 ||
        std::abs(distance - length) > 1e-4 * distance)
        throw std::runtime_error("UI gradient requires a consistent nonzero resolved line length");
    return copy_stops(
        Kind::Linear,
        {start[0], start[1], float(dx / (distance * distance)), float(dy / (distance * distance))},
        repeating, stops);
}
Gradient make_radial_gradient(std::array<float, 2> center, std::array<float, 2> radius,
                              bool repeating, std::span<const Stop> stops) {
    validate_point(center);
    for (const auto value : radius)
        if (!std::isfinite(value) || value < 1e-4 || value > 65536)
            throw std::runtime_error(
                "UI radial gradient radii require finite values within 0.0001..65536 pixels");
    return copy_stops(Kind::Radial, {center[0], center[1], 1.f / radius[0], 1.f / radius[1]},
                      repeating, stops);
}
Gradient make_conic_gradient(std::array<float, 2> center, double angle, bool repeating,
                             std::span<const Stop> stops) {
    validate_point(center);
    if (!std::isfinite(angle) || std::abs(angle) > 65536)
        throw std::runtime_error(
            "UI conic gradient angle requires a finite value within -65536..65536 radians");
    const auto turns =
        std::remainder(angle, 2 * std::numbers::pi_v<double>) / (2 * std::numbers::pi_v<double>);
    return copy_stops(Kind::Conic, {center[0], center[1], float(turns), 0}, repeating, stops);
}
} // namespace forge::ui_gradient_detail
