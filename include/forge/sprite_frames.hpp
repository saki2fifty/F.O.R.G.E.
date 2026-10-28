#pragma once
#include <cstddef>
#include <cstdint>

namespace forge {
inline constexpr std::size_t sprite_frame_region_limit = 512;
inline constexpr std::size_t sprite_frame_value_byte_limit = 128 * 1024;
// Ordered per-object image frames, measured from the current image crop's top-left.
// Extents are positive pixels; unlike an image crop, zero never means "to edge".
struct SpriteFrameRegion {
    std::uint32_t x = 0, y = 0, width = 1, height = 1;
    // Stored pixels may be turned clockwise by one quarter turn. Width/height
    // still describe their physical rectangle in the texture.
    bool rotated_clockwise = false;
    // Logical image canvas before transparent edges were removed. Zero infers
    // that retained axis; offsets must fit the independently resolved canvas.
    std::uint32_t source_width = 0, source_height = 0, trim_x = 0, trim_y = 0;
    bool operator==(const SpriteFrameRegion&) const = default;
};
} // namespace forge
