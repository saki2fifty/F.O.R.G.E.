#pragma once
#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
namespace forge {
inline constexpr unsigned reflection_source_level_limit = 6;
struct ReflectionSourceExtent {
    unsigned width = 0, height = 0;
    bool operator==(const ReflectionSourceExtent&) const = default;
};
struct ReflectionSourcePlan {
    std::array<ReflectionSourceExtent, reflection_source_level_limit> levels{};
    unsigned count = 0;
    std::uint64_t bytes = 0;
};
// Six2–64px source scales cover a projected radius capped at24px.
// RGBA16F premultiplied radiance/coverage plus RG32F depth bounds, view owned.
inline ReflectionSourcePlan reflection_source_plan(unsigned width, unsigned height) {
    if (!width || !height)
        throw std::invalid_argument("Reflection source extent must be nonzero");
    ReflectionSourcePlan result;
    while ((width > 1 || height > 1) && result.count < result.levels.size()) {
        width = width / 2 + width % 2;
        height = height / 2 + height % 2;
        const auto pixels = std::uint64_t(width) * height;
        if (pixels > (std::numeric_limits<std::uint64_t>::max() - result.bytes) / 16)
            throw std::overflow_error("Reflection source byte count exceeds its range");
        result.bytes += pixels * 16;
        result.levels[result.count++] = {width, height};
    }
    return result;
}
} // namespace forge
