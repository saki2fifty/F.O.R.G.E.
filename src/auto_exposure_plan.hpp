#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>

namespace forge {
inline constexpr unsigned auto_exposure_bins = 128, auto_exposure_meter_side = 256;
inline constexpr unsigned auto_exposure_tile_x = 16, auto_exposure_tile_y = 8;
inline constexpr float auto_exposure_log_minimum = -16, auto_exposure_log_maximum = 16;
inline constexpr std::size_t auto_exposure_tile_count =
    (auto_exposure_meter_side / auto_exposure_tile_x) *
    (auto_exposure_meter_side / auto_exposure_tile_y);
inline constexpr std::size_t auto_exposure_histogram_bytes =
    auto_exposure_tile_count * auto_exposure_bins * 4;
inline constexpr std::size_t auto_exposure_payload_bytes = auto_exposure_histogram_bytes + 16 + 64;
struct AutoExposurePlan {
    unsigned width, height, sample_width, sample_height, groups_x, groups_y;
};
// A bounded deterministic center grid. Pixel-address arithmetic must remain
// representable in the shader's uint32, independently of the native device limit.
inline AutoExposurePlan auto_exposure_plan(unsigned width, unsigned height,
                                           unsigned device_maximum) {
    constexpr auto arithmetic_maximum =
        std::numeric_limits<std::uint32_t>::max() / (2 * auto_exposure_meter_side);
    if (!width || !height || !device_maximum || width > device_maximum || height > device_maximum ||
        width > arithmetic_maximum || height > arithmetic_maximum)
        throw std::invalid_argument(
            "Automatic exposure source dimensions exceed the device or pixel-address profile");
    const auto x = std::min(width, auto_exposure_meter_side),
               y = std::min(height, auto_exposure_meter_side);
    return {width,
            height,
            x,
            y,
            (x + auto_exposure_tile_x - 1) / auto_exposure_tile_x,
            (y + auto_exposure_tile_y - 1) / auto_exposure_tile_y};
}
struct AutoExposureTiming {
    float delta = 0;
    bool reset = true;
};
// Borrow the host's existing unscaled presentation timestamp. No simulation
// clock or hidden wall-time sampling. A rewind starts a new visual history;
// a long interruption contributes at most one quarter second of adaptation.
inline AutoExposureTiming auto_exposure_timing(std::optional<double> previous, double current) {
    if (!std::isfinite(current) || current < 0 ||
        (previous && (!std::isfinite(*previous) || *previous < 0)))
        throw std::invalid_argument(
            "Automatic exposure needs finite nonnegative presentation time");
    if (!previous || current < *previous)
        return {};
    return {float(std::min(.25, current - *previous)), false};
}
} // namespace forge
