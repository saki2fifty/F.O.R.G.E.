#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <forge/audio_components.hpp>
#include <limits>
#include <optional>
#include <stdexcept>

namespace forge::detail {
// Per-voice scheduling intent only. The existing native engine owns the clock.
class AudioStartDelay {
    std::uint64_t remaining_ = 0;
    std::optional<std::uint64_t> due_;

  public:
    AudioStartDelay() = default;
    AudioStartDelay(double seconds, std::uint32_t rate) {
        if (!std::isfinite(seconds) || seconds < 0 || seconds > sound_cue_delay_limit_seconds ||
            !rate)
            throw std::runtime_error("Audio start delay requires 0..10 seconds and an output rate");
        remaining_ = std::uint64_t(std::llround(seconds * rate));
    }
    bool waiting() const { return remaining_ != 0 || due_.has_value(); }
    bool unarmed() const { return remaining_ != 0; }
    std::uint64_t remaining(std::uint64_t now) const {
        return due_ ? *due_ - std::min(*due_, now) : remaining_;
    }
    std::uint64_t arm(std::uint64_t now) {
        if (!remaining_ || now > std::numeric_limits<std::uint64_t>::max() - remaining_)
            throw std::runtime_error("Audio delayed start cannot be represented on its clock");
        due_ = now + remaining_;
        remaining_ = 0;
        return *due_;
    }
    // Returns true only when silence remains; an already started voice keeps its cursor.
    bool suspend(std::uint64_t now) {
        if (!due_)
            return false;
        remaining_ = *due_ - std::min(*due_, now);
        due_.reset();
        return remaining_ != 0;
    }
};
} // namespace forge::detail
