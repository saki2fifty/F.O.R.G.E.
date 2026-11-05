#include "ui_gradient_profile.hpp"
#include <cmath>
#include <stdexcept>

namespace forge::ui_gradient_detail {
Gradient make_linear_gradient(std::array<float, 2> start, std::array<float, 2> end, double length,
                              bool repeating, std::span<const Stop> stops) {
    for (const auto point : {start, end})
        for (const auto value : point)
            if (!std::isfinite(value) || std::abs(value) > 65536)
                throw std::runtime_error(
                    "UI gradient points require finite coordinates within -65536..65536 pixels");
    const double dx = double(end[0]) - start[0], dy = double(end[1]) - start[1];
    const double distance = std::hypot(dx, dy);
    if (!std::isfinite(length) || length < 1e-4 || distance < 1e-4 ||
        std::abs(distance - length) > 1e-4 * distance)
        throw std::runtime_error("UI gradient requires a consistent nonzero resolved line length");
    if (stops.size() < 2 || stops.size() > max_stops)
        throw std::runtime_error("UI linear gradients require 2..16 resolved color stops");
    Gradient result;
    result.line = {start[0], start[1], float(dx / (distance * distance)),
                   float(dy / (distance * distance))};
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
} // namespace forge::ui_gradient_detail
