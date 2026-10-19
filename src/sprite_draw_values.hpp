#pragma once
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
};
inline void validate_sprite_draw_values(const SpriteDrawValues& values) {
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
