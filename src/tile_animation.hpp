#pragma once
#include "tile_set_values.hpp"
#include <cmath>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace forge::tile_detail {
inline double tile_animation_duration(std::span<const TileAnimationFrame> frames,
                                      std::uint64_t image_frames) {
    if (frames.empty() || frames.size() > tile_animation_frame_limit || !image_frames ||
        image_frames > UINT32_MAX)
        throw std::invalid_argument("Tile animation needs1..64frames and a valid image grid");
    double duration = 0;
    for (const auto& value : frames) {
        if (value.frame >= image_frames || !std::isfinite(value.seconds) || value.seconds < .001 ||
            value.seconds > 600)
            throw std::invalid_argument("Tile animation frame/duration is outside its bounds");
        duration += value.seconds;
    }
    return duration;
}
// Select from a validated immutable sequence. Input is simulation time, never a
// renderer/editor wall clock. No frame counter, source mutation or retained cache.
inline std::uint32_t tile_animation_frame_at(std::span<const TileAnimationFrame> frames,
                                             double duration, std::uint32_t fallback,
                                             double seconds) {
    if (!std::isfinite(seconds) || seconds < 0)
        throw std::invalid_argument("Tile animation time must be finite and nonnegative");
    if (frames.empty())
        return fallback;
    if (!std::isfinite(duration) || duration <= 0)
        throw std::invalid_argument("Tile animation duration is invalid");
    const double phase = std::fmod(seconds, duration);
    double boundary = 0;
    for (const auto& frame : frames) {
        boundary += frame.seconds;
        if (phase < boundary)
            return frame.frame;
    }
    // Rounding at the final cumulative boundary stays inside the same cycle.
    return frames.back().frame;
}
inline std::uint32_t tile_frame_at(const TileSetDefinition& tile, double seconds) {
    const auto& animation = tile.animation;
    return animation && animation->enabled
               ? tile_animation_frame_at(animation->frames, animation->duration, tile.frame,
                                         seconds)
               : tile_animation_frame_at({}, 0, tile.frame, seconds);
}
} // namespace forge::tile_detail
