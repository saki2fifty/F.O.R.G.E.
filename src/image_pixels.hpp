#pragma once
#include <cstdint>
#include <variant>
#include <vector>

namespace forge {
// Detached top-to-bottom RGBA8 rows, without a device or source ownership.
// The producing operation defines the color and alpha interpretation.
struct Rgba8Pixels {
    unsigned width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
    bool srgb = true; // Label only; the bytes are not converted.
};
// Linear working RGB with sRGB/Rec.709 primaries. Alpha is deliberately absent;
// callers must verify that a source can be represented as an opaque image.
struct Rgb32Pixels {
    unsigned width = 0, height = 0;
    std::vector<float> rgb;
};
using ImagePixels = std::variant<Rgba8Pixels, Rgb32Pixels>;
} // namespace forge
