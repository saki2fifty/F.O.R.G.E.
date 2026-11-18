#include "game_recording.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace forge {
GameRecording::GameRecording(double fps, unsigned frames)
    : rate_(asset_detail::avi_movie_frame_rate(fps)), planned_(frames) {
    asset_detail::validate_avi_game_timing(rate_, frames);
}
RuntimeClock::Time GameRecording::at_sample(std::uint64_t sample) {
    // Round upwards once at each cumulative boundary. The media cursor is bounded
    // to128MiB stereo PCM, so multiplication and signed nanoseconds remain bounded.
    const auto ns = (sample * 1000000000 + asset_detail::movie_audio_sample_rate - 1) /
                    asset_detail::movie_audio_sample_rate;
    return RuntimeClock::Time{} + std::chrono::nanoseconds(ns);
}
RuntimeClock::Time GameRecording::time() const { return at_sample(samples_); }
Json GameRecording::status() const {
    Json result{{"mode", "offline_game"},
                {"started", true},
                {"complete", completed_ == planned_ && !failed_},
                {"rate", rate_.rate},
                {"scale", rate_.scale},
                {"planned_frames", planned_},
                {"completed_frames", completed_},
                {"audio_frames", samples_},
                {"sample_rate", asset_detail::movie_audio_sample_rate},
                {"seconds", double(samples_) / asset_detail::movie_audio_sample_rate},
                {"failed", failed()}};
    if (frame_pending_) {
        result["pending_frame"] = completed_;
        result["pending_audio_frames"] = frame_pcm_.size() / 4;
    }
    return result;
}
std::vector<std::byte> GameRecording::read_frame(const Mix& mix, const Advance& advance) {
    for (;;) {
        auto result = poll_frame(mix, advance, {});
        if (result)
            return std::move(*result);
    }
}
std::optional<std::vector<std::byte>>
GameRecording::poll_frame(const Mix& mix, const Advance& advance, const Ready& ready) {
    if (failed_ || busy_ || completed_ == planned_ || !mix || !advance)
        throw std::runtime_error("Game recording has no available frame interval");
    const auto end = asset_detail::avi_movie_audio_frames(rate_, completed_ + 1);
    if (!frame_pending_) {
        frame_pcm_.reserve(std::size_t(end - samples_) * 4);
        frame_pending_ = true;
    }
    struct Busy {
        bool& value;
        explicit Busy(bool& target) : value(target) { value = true; }
        ~Busy() { value = false; }
    } busy(busy_);
    try {
        std::array<float, audio_quantum * 2> buffer{};
        unsigned work = 0;
        while (samples_ < end && work < poll_quantum_limit) {
            if (ready && !ready())
                return std::nullopt;
            const auto count = std::min<std::uint64_t>(buffer.size() / 2, end - samples_);
            const auto output = std::span(buffer).first(std::size_t(count) * 2);
            std::fill(output.begin(), output.end(), 0.f);
            mix(output);
            for (const auto value : output) {
                if (!std::isfinite(value))
                    throw std::runtime_error("Game recording mixer produced nonfinite PCM");
                const auto integer = std::clamp(std::lround(std::clamp(value, -1.f, 1.f) * 32768.f),
                                                -32768l, 32767l);
                const auto bits = std::uint16_t(std::int16_t(integer));
                frame_pcm_.push_back(std::byte(bits & 255));
                frame_pcm_.push_back(std::byte(bits >> 8));
            }
            advance(at_sample(samples_ + count));
            samples_ += count;
            ++work;
        }
        // A Scene change on the last quantum still needs admission before the
        // next image can be captured. Do not remix its already consumed audio.
        if (samples_ != end || (ready && !ready()))
            return std::nullopt;
        ++completed_;
        frame_pending_ = false;
        auto result = std::move(frame_pcm_);
        frame_pcm_ = {};
        return result;
    } catch (...) {
        // Native work cannot be rewound after a failed mix/advance. Discard only
        // this unpublished interval and keep the recording terminally failed.
        failed_ = true;
        frame_pending_ = false;
        frame_pcm_.clear();
        throw;
    }
}
} // namespace forge
