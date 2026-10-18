#pragma once
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <stdexcept>
#include <string>
namespace forge::ui {
// Editor-only sampling settings. AnimationRuntime remains the sequence time owner.
struct SequenceFramePlan {
    static constexpr int maximum_frames = 4096;
    double start_seconds = 0, frames_per_second = 30;
    int frame_count = 180;
    double sample(std::size_t index) const {
        if (!std::isfinite(start_seconds) || start_seconds < 0 ||
            !std::isfinite(frames_per_second) || frames_per_second <= 0 ||
            frames_per_second > 240 || frame_count < 1 || frame_count > maximum_frames ||
            index >= static_cast<std::size_t>(frame_count))
            throw std::runtime_error("Choose a nonnegative start, FPS above zero up to 240, and "
                                     "1 to 4096 frames");
        const auto time = start_seconds + static_cast<double>(index) / frames_per_second;
        if (!std::isfinite(time))
            throw std::runtime_error("Sequence frame time exceeds its numeric range");
        return time;
    }
    void validate(double duration) const {
        // Validate count before subtracting: even an INT_MIN draft must be safe.
        (void)sample(0);
        if (!std::isfinite(duration) || duration <= 0 ||
            sample(static_cast<std::size_t>(frame_count - 1)) >= duration)
            throw std::runtime_error("Every frame must be before the sequence duration; "
                                     "reduce the start or frame count");
    }
    std::string filename(std::size_t index) const {
        (void)sample(index);
        char name[32];
        std::snprintf(name, sizeof(name), "frame-%06u.png", static_cast<unsigned>(index));
        return name;
    }
};
} // namespace forge::ui
