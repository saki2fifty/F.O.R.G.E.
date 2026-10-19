#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace forge {
// Detached numeric mapping. No texture, allocation, identity or retained component.
struct SpriteImageAxis {
    double source_low = 0, source_high = 0;
    double target_low = 0, target_high = 0;
    double repeat = 1;
};
struct SpriteImageMapping {
    std::uint32_t mode = 0; // Matches SpriteImageMode; zero preserves the affine path.
    std::array<SpriteImageAxis, 2> axes;
};
inline SpriteImageAxis sprite_image_axis(std::uint32_t mode, std::uint32_t source_pixels,
                                         std::uint32_t low_border, std::uint32_t high_border,
                                         double meters, double pixels_per_meter) {
    if (mode > 2 || !source_pixels || !std::isfinite(meters) || meters < .0001 || meters > 10000 ||
        !std::isfinite(pixels_per_meter) || pixels_per_meter < .0001 || pixels_per_meter > 1000000)
        throw std::invalid_argument("Sprite image mapping has invalid mode, extent or density");
    if (mode == 0)
        return {};
    const double target_pixels = meters * pixels_per_meter;
    if (mode == 2) {
        // Bound float32 repeat phase in source pixels, independently of atlas offset.
        // This is not a claim of arbitrary precision for unbounded repeated fills.
        if (target_pixels > 1048576)
            throw std::invalid_argument("Sprite repeated fill exceeds 1048576 target pixels");
        SpriteImageAxis result;
        result.repeat = target_pixels / source_pixels;
        return result;
    }
    const auto borders = std::uint64_t(low_border) + high_border;
    if (borders >= source_pixels)
        throw std::invalid_argument("Sprite sliced borders must leave positive source pixels");
    const double span = std::max(target_pixels, double(borders));
    return {double(low_border) / source_pixels, double(high_border) / source_pixels,
            low_border / span, high_border / span, 1};
}
} // namespace forge
