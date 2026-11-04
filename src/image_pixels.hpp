#pragma once
#include <cstdint>
#include <vector>

namespace forge {
// Detached top-to-bottom RGBA8 rows, without a device or source ownership.
// The producing operation defines the color and alpha interpretation.
struct Rgba8Pixels {
    unsigned width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
    bool srgb = true; // Label only; the bytes are not converted.
};
} // namespace forge
