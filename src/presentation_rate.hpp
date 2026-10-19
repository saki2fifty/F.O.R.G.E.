#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>
namespace forge {
// Observes completed host presentations. Never drives simulation or owns a clock.
class PresentationRate {
  public:
    using Clock = std::chrono::steady_clock;
    struct Sample {
        double fps = 0, mean_ms = 0, peak_ms = 0;
    };
    void reset() {
        last_.reset();
        sample_.reset();
        intervals_ = 0;
        peak_seconds_ = 0;
    }
    const std::optional<Sample>& sample() const { return sample_; }
    // True when a reading changes, including invalid time clearing an old reading.
    bool presented(Clock::time_point now) {
        if (!last_) {
            last_ = start_ = now;
            return false;
        }
        if (now <= *last_ || intervals_ == std::numeric_limits<std::uint64_t>::max()) {
            const bool changed = sample_.has_value();
            reset();
            last_ = start_ = now;
            return changed;
        }
        peak_seconds_ = std::max(peak_seconds_, seconds(now, *last_));
        last_ = now;
        ++intervals_;
        const auto elapsed = seconds(now, start_);
        if (elapsed < .5L)
            return false;
        sample_ = Sample{double(intervals_ / elapsed), double(elapsed * 1000 / intervals_),
                         double(peak_seconds_ * 1000)};
        intervals_ = 0;
        peak_seconds_ = 0;
        start_ = now;
        return true;
    }

  private:
    static long double seconds(Clock::time_point later, Clock::time_point earlier) {
        using Rep = Clock::duration::rep;
        static_assert(std::is_integral_v<Rep>);
        using Unsigned = std::make_unsigned_t<Rep>;
        // Ordered unsigned subtraction also admits min→max without signed overflow.
        const auto ticks = Unsigned(later.time_since_epoch().count()) -
                           Unsigned(earlier.time_since_epoch().count());
        return static_cast<long double>(ticks) * Clock::period::num / Clock::period::den;
    }
    std::optional<Clock::time_point> last_;
    Clock::time_point start_{};
    std::uint64_t intervals_ = 0;
    long double peak_seconds_ = 0;
    std::optional<Sample> sample_;
};
} // namespace forge
