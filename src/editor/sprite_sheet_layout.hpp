#pragma once
#include "../sprite_region.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace forge {
// Disposable editor view math. Texture/grid admission remains in sprite_region;
// a hit proposes the existing U32 frame value, never a new atlas record.
inline std::optional<std::uint32_t> sprite_sheet_frame_at(std::uint32_t width, std::uint32_t height,
                                                          const SpriteGridLayout& grid, double x,
                                                          double y) {
    const auto first = sprite_grid_region(width, height, grid, 0);
    if (!std::isfinite(x) || !std::isfinite(y) || x < grid.margin_x || y < grid.margin_y ||
        x >= width || y >= height)
        return {};
    x -= grid.margin_x;
    y -= grid.margin_y;
    const auto pitch_x = std::uint64_t(first.width) + grid.spacing_x;
    const auto pitch_y = std::uint64_t(first.height) + grid.spacing_y;
    const auto column = std::uint64_t(x / pitch_x), row = std::uint64_t(y / pitch_y);
    if (column >= grid.columns || row >= grid.rows || x - column * pitch_x >= first.width ||
        y - row * pitch_y >= first.height)
        return {};
    const auto frame = row * grid.columns + column;
    return frame <= UINT32_MAX ? std::optional<std::uint32_t>(std::uint32_t(frame)) : std::nullopt;
}

struct SpriteSheetView {
    double zoom = 1, center_x = .5, center_y = .5;
    bool operator==(const SpriteSheetView&) const = default;
};
struct SpriteSheetViewport {
    double left{}, top{}, width{}, height{};
    std::array<double, 4> region{}; // Top-left normalized crop: minX,minY,maxX,maxY.
    std::uint32_t texture_width{}, texture_height{};
    std::optional<std::array<double, 2>> pixel_at(double x, double y) const {
        if (!std::isfinite(x) || !std::isfinite(y) || x < left || y < top || x >= left + width ||
            y >= top + height)
            return {};
        return std::array<double, 2>{
            (region[0] + (x - left) / width * (region[2] - region[0])) * texture_width,
            (region[1] + (y - top) / height * (region[3] - region[1])) * texture_height};
    }
};
inline SpriteSheetViewport sprite_sheet_viewport(std::uint32_t width, std::uint32_t height,
                                                 double canvas_width, double canvas_height,
                                                 const SpriteSheetView& view) {
    if (!width || !height || !std::isfinite(canvas_width) || !std::isfinite(canvas_height) ||
        canvas_width <= 0 || canvas_height <= 0 || !std::isfinite(view.zoom) || view.zoom < 1 ||
        view.zoom > 128 || !std::isfinite(view.center_x) || !std::isfinite(view.center_y))
        throw std::invalid_argument("Invalid Sprite sheet view");
    const auto scale = std::min(canvas_width / width, canvas_height / height) * view.zoom;
    const auto virtual_width = width * scale, virtual_height = height * scale;
    if (!std::isfinite(virtual_width) || !std::isfinite(virtual_height) || virtual_width <= 0 ||
        virtual_height <= 0)
        throw std::invalid_argument("Sprite sheet view extent exceeds its numerical range");
    const auto visible_width = std::min(canvas_width, virtual_width);
    const auto visible_height = std::min(canvas_height, virtual_height);
    const auto fraction_x = visible_width / virtual_width;
    const auto fraction_y = visible_height / virtual_height;
    const auto center_x = std::clamp(view.center_x, fraction_x / 2, 1 - fraction_x / 2);
    const auto center_y = std::clamp(view.center_y, fraction_y / 2, 1 - fraction_y / 2);
    const auto left = (canvas_width - visible_width) / 2;
    const auto top = (canvas_height - visible_height) / 2;
    if (!(left + visible_width > left) || !(top + visible_height > top))
        throw std::invalid_argument("Sprite sheet image is smaller than its view precision");
    return {left,
            top,
            visible_width,
            visible_height,
            {center_x - fraction_x / 2, center_y - fraction_y / 2, center_x + fraction_x / 2,
             center_y + fraction_y / 2},
            width,
            height};
}
inline void sprite_sheet_clamp_center(SpriteSheetView& view, const SpriteSheetViewport& viewport) {
    view.center_x = (viewport.region[0] + viewport.region[2]) / 2;
    view.center_y = (viewport.region[1] + viewport.region[3]) / 2;
}
inline bool sprite_sheet_pan(SpriteSheetView& view, std::uint32_t width, std::uint32_t height,
                             double canvas_width, double canvas_height, double dx, double dy) {
    if (!std::isfinite(dx) || !std::isfinite(dy))
        return false;
    const auto previous = sprite_sheet_viewport(width, height, canvas_width, canvas_height, view);
    auto candidate = view;
    sprite_sheet_clamp_center(candidate, previous);
    candidate.center_x -= dx / previous.width * (previous.region[2] - previous.region[0]);
    candidate.center_y -= dy / previous.height * (previous.region[3] - previous.region[1]);
    if (!std::isfinite(candidate.center_x) || !std::isfinite(candidate.center_y))
        return false;
    sprite_sheet_clamp_center(
        candidate, sprite_sheet_viewport(width, height, canvas_width, canvas_height, candidate));
    const bool changed = candidate != view;
    view = candidate;
    return changed;
}
inline bool sprite_sheet_zoom_at(SpriteSheetView& view, std::uint32_t width, std::uint32_t height,
                                 double canvas_width, double canvas_height, double x, double y,
                                 double wheel) {
    if (!std::isfinite(wheel))
        return false;
    const auto previous = sprite_sheet_viewport(width, height, canvas_width, canvas_height, view);
    const auto anchor = previous.pixel_at(x, y);
    if (!anchor)
        return false;
    auto candidate = view;
    candidate.zoom = std::clamp(view.zoom * std::exp(std::clamp(wheel, -20., 20.) * .15), 1., 128.);
    if (candidate.zoom == view.zoom)
        return false;
    const auto next = sprite_sheet_viewport(width, height, canvas_width, canvas_height, candidate);
    candidate.center_x = (*anchor)[0] / width -
                         ((x - next.left) / next.width - .5) * (next.region[2] - next.region[0]);
    candidate.center_y = (*anchor)[1] / height -
                         ((y - next.top) / next.height - .5) * (next.region[3] - next.region[1]);
    sprite_sheet_clamp_center(
        candidate, sprite_sheet_viewport(width, height, canvas_width, canvas_height, candidate));
    view = candidate;
    return true;
}
} // namespace forge
