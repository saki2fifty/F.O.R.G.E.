#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <forge/render_view.hpp>
#include <forge/scene_render_settings.hpp>
#include <limits>
#include <stdexcept>

namespace forge {
inline constexpr std::uint64_t depth_of_field_payload_limit = 128ull * 1024 * 1024;
struct DepthOfFieldPlan {
    PixelViewport area;
    std::array<float, 4> projection;
    std::uint64_t bytes;
};
inline DepthOfFieldPlan depth_of_field_plan(unsigned width, unsigned height, const CameraView& view,
                                            unsigned maximum, std::uint64_t budget) {
    const auto& area = view.viewport;
    if (!area.width || !area.height || width > maximum || height > maximum || area.x >= width ||
        area.y >= height || area.width > width - area.x || area.height > height - area.y ||
        std::uint64_t(area.width) > budget / 16 / area.height)
        throw std::invalid_argument("Depth of field camera rectangle exceeds target or budget");
    const std::array p{view.projection[10], view.projection[11], view.projection[14],
                       view.projection[15]};
    if (!std::all_of(p.begin(), p.end(), [](float x) { return std::isfinite(x); }) ||
        double(p[0]) * p[3] <= double(p[1]) * p[2])
        throw std::invalid_argument("Depth of field requires a finite depth projection");
    const auto distance = [&](double depth) {
        return (double(p[1]) - depth * p[3]) / (depth * p[2] - p[0]);
    };
    const auto near_distance = distance(0);
    const auto far_distance = p[2] == p[0] ? std::numeric_limits<double>::infinity() : distance(1);
    if (!std::isfinite(near_distance) || std::isnan(far_distance) || near_distance <= 0 ||
        far_distance <= near_distance)
        throw std::invalid_argument("Depth of field requires positive forward near/far");
    return {area, p, std::uint64_t(area.width) * area.height * 16};
}
} // namespace forge
