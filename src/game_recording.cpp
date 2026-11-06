#include "game_recording.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

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
    return {{"mode", "offline_game"},
            {"started", true},
            {"complete", completed_ == planned_ && !failed_},
            {"rate", rate_.rate},
            {"scale", rate_.scale},
            {"planned_frames", planned_},
            {"completed_frames", completed_},
            {"audio_frames", samples_},
            {"sample_rate", asset_detail::movie_audio_sample_rate},
            {"seconds", double(samples_) / asset_detail::movie_audio_sample_rate},
            {"failed", failed_}};
}
std::vector<std::byte> GameRecording::read_frame(const Mix& mix, const Advance& advance) {
    if (failed_ || completed_ == planned_ || !mix || !advance)
        throw std::runtime_error("Game recording has no available frame interval");
    const auto end = asset_detail::avi_movie_audio_frames(rate_, completed_ + 1);
    std::vector<std::byte> result;
    result.reserve(std::size_t(end - samples_) * 4);
    // Once mixing/advancing starts, an error cannot be retried against arbitrary
    // C++ state. Keep this recording failed and let the host retire the fresh run.
    failed_ = true;
    std::array<float, audio_quantum * 2> buffer{};
    while (samples_ < end) {
        const auto count = std::min<std::uint64_t>(buffer.size() / 2, end - samples_);
        const auto output = std::span(buffer).first(std::size_t(count) * 2);
        std::fill(output.begin(), output.end(), 0.f);
        mix(output);
        for (const auto value : output) {
            if (!std::isfinite(value))
                throw std::runtime_error("Game recording mixer produced nonfinite PCM");
            const auto integer =
                std::clamp(std::lround(std::clamp(value, -1.f, 1.f) * 32768.f), -32768l, 32767l);
            const auto bits = std::uint16_t(std::int16_t(integer));
            result.push_back(std::byte(bits & 255));
            result.push_back(std::byte(bits >> 8));
        }
        advance(at_sample(samples_ + count));
        samples_ += count;
    }
    ++completed_;
    failed_ = false;
    return result;
}
} // namespace forge
