#pragma once
#include <cstddef>
#include <cstdint>

namespace forge {
inline constexpr std::size_t sprite_frame_region_limit = 512;
// Ordered per-object image frames, measured from the current image crop's top-left.
// Extents are positive pixels; unlike an image crop, zero never means "to edge".
struct SpriteFrameRegion {
    std::uint32_t x = 0, y = 0, width = 1, height = 1;
    bool operator==(const SpriteFrameRegion&) const = default;
};
} // namespace forge
