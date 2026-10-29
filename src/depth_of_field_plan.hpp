#pragma once
#include "render_projection.hpp"
#include <array>
#include <forge/render_view.hpp>
#include <forge/scene_render_settings.hpp>
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
    validate_forward_depth_projection(p);
    return {area, p, std::uint64_t(area.width) * area.height * 16};
}
} // namespace forge
