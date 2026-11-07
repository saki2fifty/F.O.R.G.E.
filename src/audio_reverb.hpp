#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace forge::detail {
// Private bounded diffusion. Construction owns every allocation; processing
// touches only preallocated samples. The audio runtime remains the graph owner.
class AudioReverb {
  public:
    AudioReverb(unsigned rate, unsigned channels, float decay, float damping, float wet)
        : channels_(channels), damping_(damping), wet_(wet) {
        if (rate < 8000 || rate > 192000 || channels < 1 || channels > 2 || !std::isfinite(decay) ||
            decay < .1f || decay > 10 || !std::isfinite(damping) || damping < 0 || damping > .95f ||
            !std::isfinite(wet) || wet < 0 || wet > 1)
            throw std::runtime_error("Invalid bounded source reverb configuration");
        constexpr std::array seconds{.0297, .0371, .0411, .0437};
        for (unsigned channel = 0; channel < channels; ++channel)
            for (unsigned i = 0; i < seconds.size(); ++i) {
                auto& line = lines_[channel][i];
                const auto frames =
                    std::size_t(std::llround(rate * (seconds[i] + .0017 * channel)));
                line.samples.resize(frames);
                // Each traversal attenuates according to its own duration. The
                // orthogonal feedback matrix preserves energy before this loss.
                line.gain = float(std::pow(.001, double(frames) / (rate * double(decay))));
                bytes_ += line.samples.capacity() * sizeof(float);
            }
        // Owner-observed EOS, like existing echo tails: conservative retirement,
        // not a second sample clock or a claim of sample-accurate source expiry.
        tail_frames_ = std::uint64_t(std::ceil(rate * (2 * double(decay) + .5)));
    }
    AudioReverb(const AudioReverb&) = delete;
    AudioReverb& operator=(const AudioReverb&) = delete;
    std::size_t bytes() const noexcept { return bytes_; }
    std::uint64_t tail_frames() const noexcept { return tail_frames_; }
    void clear() noexcept {
        for (auto& channel : lines_)
            for (auto& line : channel) {
                std::fill(line.samples.begin(), line.samples.end(), 0);
                line.cursor = 0;
                line.filtered = 0;
            }
    }
    // Null input feeds silence while an EOS tail drains. Input/output may alias.
    void process(const float* input, float* output, std::size_t frames) noexcept {
        for (std::size_t frame = 0; frame < frames; ++frame)
            for (unsigned channel = 0; channel < channels_; ++channel) {
                const auto index = frame * channels_ + channel;
                const float sample = input ? finite(input[index]) : 0;
                auto& lines = lines_[channel];
                std::array<float, 4> delayed;
                for (unsigned i = 0; i < lines.size(); ++i) {
                    auto& line = lines[i];
                    line.filtered = finite((1 - damping_) * line.samples[line.cursor] +
                                           damping_ * line.filtered);
                    delayed[i] = line.filtered;
                }
                const auto [a, b, c, d] = delayed;
                const std::array feedback{(a + b + c + d) * .5f, (a - b + c - d) * .5f,
                                          (a + b - c - d) * .5f, (a - b - c + d) * .5f};
                for (unsigned i = 0; i < lines.size(); ++i) {
                    auto& line = lines[i];
                    line.samples[line.cursor] = finite(sample * .5f + feedback[i] * line.gain);
                    if (++line.cursor == line.samples.size())
                        line.cursor = 0;
                }
                output[index] = finite((1 - wet_) * sample + wet_ * (a + b + c + d) * .25f);
            }
    }

  private:
    struct Line {
        std::vector<float> samples;
        std::size_t cursor = 0;
        float filtered = 0, gain = 0;
    };
    std::array<std::array<Line, 4>, 2> lines_;
    unsigned channels_;
    float damping_, wet_;
    std::size_t bytes_ = 0;
    std::uint64_t tail_frames_ = 0;
    static float finite(float value) noexcept {
        return std::isfinite(value) && std::abs(value) >= 1e-20f ? value : 0;
    }
};
} // namespace forge::detail
