#pragma once
#include <array>
#include <cstdint>
#include <forge/sprite_frames.hpp>
#include <optional>
#include <span>
#include <stdexcept>

namespace forge {
// Private value math for the Sprite consumer. Texture coordinates follow ADR009:
// rows start at the top. These values introduce no persisted component or asset.
struct SpritePixelRegion {
    std::uint32_t x = 0, y = 0, width = 0, height = 0;
    bool operator==(const SpritePixelRegion&) const = default;
};
struct SpriteGridLayout {
    std::uint32_t columns = 1, rows = 1;
    std::uint32_t margin_x = 0, margin_y = 0, spacing_x = 0, spacing_y = 0;
};
inline SpritePixelRegion sprite_grid_region(std::uint32_t width, std::uint32_t height,
                                            const SpriteGridLayout& grid, std::uint64_t frame,
                                            std::optional<SpritePixelRegion> crop = {}) {
    auto area = crop.value_or(SpritePixelRegion{});
    if (!width || !height || area.x >= width || area.y >= height || area.width > width - area.x ||
        area.height > height - area.y)
        throw std::invalid_argument("Sprite image region must fit its texture");
    // Zero extent means the remaining image, so enabling a default region keeps
    // the whole image. Margins, spacing and frames are relative to this area.
    if (!area.width)
        area.width = width - area.x;
    if (!area.height)
        area.height = height - area.y;
    const auto axis = [](std::uint32_t extent, std::uint32_t cells, std::uint32_t margin,
                         std::uint32_t spacing) -> std::uint32_t {
        if (!extent || !cells)
            throw std::invalid_argument("Sprite texture extent and grid count must be positive");
        const auto borders = std::uint64_t(margin) * 2;
        const auto gaps = std::uint64_t(spacing) * (cells - 1);
        if (borders >= extent || gaps >= extent - borders)
            throw std::invalid_argument("Sprite grid borders/spacing leave no frame pixels");
        const auto available = extent - borders - gaps;
        if (available % cells || available / cells == 0)
            throw std::invalid_argument("Sprite grid must divide into whole positive pixel frames");
        return std::uint32_t(available / cells);
    };
    const auto cell_width = axis(area.width, grid.columns, grid.margin_x, grid.spacing_x);
    const auto cell_height = axis(area.height, grid.rows, grid.margin_y, grid.spacing_y);
    if (frame >= std::uint64_t(grid.columns) * grid.rows)
        throw std::invalid_argument("Sprite frame is outside the grid");
    // Admission above proves both final coordinates fit the original extent.
    // Keep products wide until that bounded result is converted.
    const auto x = std::uint64_t(area.x) + grid.margin_x +
                   (frame % grid.columns) * (std::uint64_t(cell_width) + grid.spacing_x);
    const auto y = std::uint64_t(area.y) + grid.margin_y +
                   (frame / grid.columns) * (std::uint64_t(cell_height) + grid.spacing_y);
    return {std::uint32_t(x), std::uint32_t(y), cell_width, cell_height};
}
inline std::uint64_t sprite_frame_count(const SpriteGridLayout& grid,
                                        std::span<const SpriteFrameRegion> regions = {}) noexcept {
    return regions.empty() ? std::uint64_t(grid.columns) * grid.rows : regions.size();
}
inline SpritePixelRegion sprite_frame_region(std::uint32_t width, std::uint32_t height,
                                             const SpriteGridLayout& grid, std::uint64_t frame,
                                             std::optional<SpritePixelRegion> crop = {},
                                             std::span<const SpriteFrameRegion> regions = {}) {
    if (regions.empty())
        return sprite_grid_region(width, height, grid, frame, crop);
    if (regions.size() > sprite_frame_region_limit || frame >= regions.size())
        throw std::invalid_argument("Sprite frame regions exceed the count or frame range");
    const auto area = sprite_grid_region(width, height, {}, 0, crop);
    // Admit the complete bank, so animation cannot expose an unchecked rectangle.
    for (const auto& region : regions)
        if (!region.width || !region.height || region.x >= area.width || region.y >= area.height ||
            region.width > area.width - region.x || region.height > area.height - region.y)
            throw std::invalid_argument("Sprite frame region must fit its image crop");
    const auto& selected = regions[std::size_t(frame)];
    return {area.x + selected.x, area.y + selected.y, selected.width, selected.height};
}
struct SpriteRegionMapping {
    std::array<double, 2> origin{}, scale{}, sample_minimum{}, sample_maximum{};
};
inline SpriteRegionMapping sprite_region_mapping(const SpritePixelRegion& region,
                                                 std::uint32_t width, std::uint32_t height,
                                                 bool flip_x = false, bool flip_y = false) {
    if (!width || !height || !region.width || !region.height || region.x >= width ||
        region.y >= height || region.width > width - region.x || region.height > height - region.y)
        throw std::invalid_argument("Sprite pixel region must fit its texture");
    SpriteRegionMapping result;
    const std::array<std::uint32_t, 2> start{region.x, region.y};
    const std::array<std::uint32_t, 2> size{region.width, region.height};
    const std::array<std::uint32_t, 2> extent{width, height};
    const std::array flip{flip_x, flip_y};
    for (unsigned axis = 0; axis < 2; ++axis) {
        result.origin[axis] = double(start[axis]) / extent[axis];
        result.scale[axis] = double(size[axis]) / extent[axis];
        // The consumer clamps after mapping, rather than shrinking the full UV
        // range and distorting every texel. These are base-level center bounds;
        // they do NOT establish safe atlas mip generation or filtering by themselves.
        result.sample_minimum[axis] = (double(start[axis]) + .5) / extent[axis];
        result.sample_maximum[axis] =
            (double(start[axis]) + double(size[axis]) - .5) / extent[axis];
        if (flip[axis]) {
            result.origin[axis] += result.scale[axis];
            result.scale[axis] = -result.scale[axis];
        }
    }
    return result;
}
} // namespace forge
