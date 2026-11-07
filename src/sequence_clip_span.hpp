#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <forge/transform_animation.hpp>
#include <span>
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

// Derived timing for a bounded ordered collection. Resource and clock ownership
// remain with the existing players; an active window reuses the pair contract.
class SequenceClipArrangement {
  public:
    static constexpr std::size_t maximum_clips = sequence_clip_limit;
    struct Clip {
        double start_seconds, duration_us;
    };
    struct Sample {
        std::size_t primary, next;
        double primary_us, next_us, weight;
    };
    static SequenceClipArrangement make(std::span<const Clip> clips) {
        if (clips.empty() || clips.size() > maximum_clips)
            throw std::runtime_error("Sequence arrangement needs 1–8 clips");
        SequenceClipArrangement result;
        result.count_ = clips.size();
        // The same validation covers the single-clip boundary without a second
        // set of duration/start rules. Its unused incoming contribution is zero.
        result.spans_[0] = SequenceClipSpan::make(clips[0].start_seconds, clips[0].start_seconds,
                                                  clips[0].duration_us, clips[0].duration_us);
        for (std::size_t i = 1; i < clips.size(); ++i) {
            result.spans_[i - 1] =
                SequenceClipSpan::make(clips[i - 1].start_seconds, clips[i].start_seconds,
                                       clips[i - 1].duration_us, clips[i].duration_us);
            if (i > 1 &&
                clips[i].start_seconds * animation_timebase <
                    clips[i - 2].start_seconds * animation_timebase + clips[i - 2].duration_us)
                throw std::runtime_error("Sequence arrangement cannot overlap three clips");
        }
        result.start_us = result.spans_[0].start_us;
        const auto& last = result.spans_[clips.size() > 1 ? clips.size() - 2 : 0];
        result.duration_us = last.start_us - result.start_us + last.duration_us;
        return result;
    }
    std::size_t size() const { return count_; }
    double relative_time(double sequence_us) const {
        if (!count_ || !std::isfinite(sequence_us))
            throw std::runtime_error("Sequence arrangement sample time is invalid");
        return std::clamp(sequence_us - start_us, 0., duration_us);
    }
    Sample sample(double relative_us) const {
        if (!count_ || !std::isfinite(relative_us))
            throw std::runtime_error("Sequence arrangement sample time is invalid");
        const auto time = std::clamp(relative_us, 0., duration_us);
        std::size_t window = 0;
        while (window + 2 < count_) {
            const auto& span = spans_[window];
            const auto complete =
                span.start_us - start_us + std::max(span.primary_duration_us, span.next_offset_us);
            if (time < complete)
                break;
            ++window;
        }
        return sample_window(window, time);
    }
    // Interval integration keeps one selected pair through its end boundary,
    // where the next window can have equal pose values but different clip IDs.
    Sample sample_window(std::size_t window, double relative_us) const {
        if (!count_ || !std::isfinite(relative_us) ||
            window >= std::max<std::size_t>(1, count_ - 1))
            throw std::runtime_error("Sequence arrangement window/time is invalid");
        const auto time = std::clamp(relative_us, 0., duration_us);
        const auto& span = spans_[window];
        const auto local = time - (span.start_us - start_us);
        return {window, count_ == 1 ? window : window + 1,
                std::clamp(local, 0., span.primary_duration_us),
                count_ == 1 ? 0.
                            : std::clamp(local - span.next_offset_us, 0., span.next_duration_us),
                count_ == 1 ? 0. : span.next_weight(local)};
    }
    bool cut_crossed(double from_us, double to_us) const {
        for (std::size_t i = 0; i + 1 < count_; ++i) {
            const auto offset = spans_[i].start_us - start_us;
            if (spans_[i].cut_crossed(from_us - offset, to_us - offset))
                return true;
        }
        return false;
    }
    double clip_offset_us(std::size_t index) const {
        if (index >= count_)
            throw std::out_of_range("Sequence clip index exceeds its arrangement");
        return index ? spans_[index - 1].start_us - start_us + spans_[index - 1].next_offset_us
                     : 0.;
    }
    double clip_duration_us(std::size_t index) const {
        if (index >= count_)
            throw std::out_of_range("Sequence clip index exceeds its arrangement");
        return index ? spans_[index - 1].next_duration_us : spans_[0].primary_duration_us;
    }
    double start_us = 0, duration_us = 0;

  private:
    std::array<SequenceClipSpan, maximum_clips - 1> spans_{};
    std::size_t count_ = 0;
};
} // namespace forge::detail
