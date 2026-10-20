#pragma once
#include "sprite_image_mapping.hpp"
#include "sprite_region.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace forge {
// Detached numeric draw values, never a resource selection or component owner.
// The draw copies these values synchronously; it retains no caller pointer.
struct SpriteDrawValues {
    SpriteRegionMapping region;
    std::array<float, 4> color{1, 1, 1, 1}; // Linear straight RGBA modulation.
    SpriteImageMapping image{};
    bool transpose_uv = false; // Swap frame-local axes before atlas flips; simple mode only.
};
inline void validate_sprite_draw_values(const SpriteDrawValues& values) {
    if (values.image.mode > 2)
        throw std::invalid_argument("Sprite draw image mode is unsupported");
    if (values.transpose_uv && values.image.mode)
        throw std::invalid_argument("Transposed Sprite UVs require the simple image mode");
    for (const auto& axis : values.image.axes) {
        for (const auto span :
             {axis.source_low, axis.source_high, axis.target_low, axis.target_high})
            if (!std::isfinite(span) || span < 0 || span > 1)
                throw std::invalid_argument("Sprite draw image spans must be finite in [0,1]");
        if (axis.source_low + axis.source_high >= 1 ||
            axis.target_low + axis.target_high > 1 + 8 * std::numeric_limits<double>::epsilon() ||
            !std::isfinite(axis.repeat) || axis.repeat <= 0 || axis.repeat > 1048576)
            throw std::invalid_argument("Sprite draw image mapping is outside supported bounds");
    }
    constexpr double tolerance = 64 * std::numeric_limits<double>::epsilon();
    for (unsigned axis = 0; axis < 2; ++axis) {
        const auto origin = values.region.origin[axis], scale = values.region.scale[axis];
        const auto low = values.region.sample_minimum[axis];
        const auto high = values.region.sample_maximum[axis];
        const auto end = origin + scale;
        if (!std::isfinite(origin) || !std::isfinite(scale) || !std::isfinite(low) ||
            !std::isfinite(high) || scale == 0 || origin < -tolerance || origin > 1 + tolerance ||
            end < -tolerance || end > 1 + tolerance || low < 0 || high > 1 || low > high ||
            low < std::min(origin, end) - tolerance || high > std::max(origin, end) + tolerance)
            throw std::invalid_argument("Sprite draw UV mapping/clamp is outside its frame");
    }
    for (auto value : values.color)
        if (!std::isfinite(value) || value < 0 || value > 1)
            throw std::invalid_argument("Sprite draw color must be finite linear RGBA in [0,1]");
}
} // namespace forge
