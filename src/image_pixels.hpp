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
    // Decoded reference texels, rather than uncompressed stored samples. This
    // provenance changes export status only, never encoding or color values.
    bool block_decoded = false;
};
// Linear working RGB with sRGB/Rec.709 primaries. Alpha is deliberately absent;
// callers must verify that a source can be represented as an opaque image.
struct Rgb32Pixels {
    unsigned width = 0, height = 0;
    std::vector<float> rgb;
    // BC6 reference decoding; final RGBE output still has its own quantization.
    bool bc6_block_decoded = false;
};
// Normalized uint16 samples, with no invented color label.
// The image encoder owns PNG byte order; source cooked bytes remain little endian.
struct Rgba16Pixels {
    unsigned width = 0, height = 0;
    std::vector<std::uint16_t> rgba;
    // Stored signed block channels mapped from [-1,1] to [0,65535].
    bool snorm_block_decoded = false;
};
using ImagePixels = std::variant<Rgba8Pixels, Rgb32Pixels, Rgba16Pixels>;
} // namespace forge
