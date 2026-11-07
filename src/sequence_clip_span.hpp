#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <forge/transform_animation.hpp>
#include <stdexcept>
namespace forge::detail {
// Derived timing only. AnimationRuntime keeps the sole sequence clock and poses.
struct SequenceClipSpan {
    double start_us, next_offset_us, primary_duration_us, next_duration_us, duration_us;
    static SequenceClipSpan make(double start_seconds, double next_start_seconds,
                                 double primary_duration, double next_duration) {
        if (!std::isfinite(start_seconds) || !std::isfinite(next_start_seconds) ||
            start_seconds < 0 || next_start_seconds < start_seconds || next_start_seconds > 3600 ||
            !std::isfinite(primary_duration) || !std::isfinite(next_duration) ||
            primary_duration < 1 || next_duration < 1 ||
            primary_duration > 3600 * animation_timebase ||
            next_duration > 3600 * animation_timebase)
            throw std::runtime_error("Sequence next clip needs ordered starts and valid durations");
        const double start = start_seconds * animation_timebase;
        const double next = next_start_seconds * animation_timebase - start;
        const double end = next + double(next_duration);
        if (end < double(primary_duration))
            throw std::runtime_error(
                "Sequence next clip must end no earlier than the primary clip");
        return {start, next, double(primary_duration), double(next_duration), end};
    }
    double relative_time(double sequence_us) const {
        return std::clamp(sequence_us - start_us, 0., duration_us);
    }
    double next_weight(double time) const {
        if (next_offset_us >= primary_duration_us)
            return time >= next_offset_us ? 1 : 0;
        return std::clamp((time - next_offset_us) / (primary_duration_us - next_offset_us), 0., 1.);
    }
    bool cut_crossed(double from, double to) const {
        return next_offset_us >= primary_duration_us && from < next_offset_us &&
               to >= next_offset_us;
    }
};
} // namespace forge::detail
