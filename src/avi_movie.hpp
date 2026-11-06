#pragma once
#include "view_image_limits.h"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <span>
#include <stdexcept>
#include <vector>

namespace forge::asset_detail {
inline constexpr std::size_t avi_movie_byte_limit = 256ull * 1024 * 1024;
inline constexpr std::size_t avi_game_audio_byte_limit = 128ull * 1024 * 1024;
inline constexpr std::uint64_t movie_rate_scale_limit = 10000000;
inline constexpr std::uint64_t movie_rate_integer_limit = std::numeric_limits<std::int32_t>::max();
struct MovieFrameRate {
    std::uint32_t rate = 0, scale = 0;
    bool operator==(const MovieFrameRate&) const = default;
};
inline bool valid_movie_frame_rate(MovieFrameRate fps) {
    return fps.rate && fps.rate <= movie_rate_integer_limit && fps.scale &&
           fps.scale <= movie_rate_scale_limit && std::gcd(fps.rate, fps.scale) == 1 &&
           std::uint64_t(fps.rate) * 1000 >= fps.scale &&
           fps.rate <= std::uint64_t(fps.scale) * 240;
}
// Container timing only. This does not change the source timeline's sample times.
// A bounded rational retains fractional rates without per-frame time rounding.
MovieFrameRate avi_movie_frame_rate(double frames_per_second);
enum class MovieAudio { None, PcmStereo48k };
inline constexpr unsigned movie_audio_sample_rate = 48000;
// Nearest whole stereo frame at a video boundary; no repeated duration rounding.
inline std::uint64_t avi_movie_audio_frames(MovieFrameRate fps, unsigned frames) {
    if (!valid_movie_frame_rate(fps))
        throw std::runtime_error("Movie FPS must be a reduced rational from 0.001 to 240");
    if (frames > FORGE_SEQUENCE_FRAME_MAX_COUNT)
        throw std::runtime_error("Movie audio boundary exceeds its frame limit");
    return (std::uint64_t(frames) * fps.scale * movie_audio_sample_rate + fps.rate / 2) / fps.rate;
}
inline void validate_avi_game_timing(MovieFrameRate fps, unsigned frames) {
    if (!frames || frames > FORGE_SEQUENCE_FRAME_MAX_COUNT || !valid_movie_frame_rate(fps) ||
        fps.rate < fps.scale || avi_movie_audio_frames(fps, frames) > avi_game_audio_byte_limit / 4)
        throw std::runtime_error(
            "Game recording requires1..240FPS,1..4096frames and at most128MiB PCM");
}

// Private bounded MJPEG AVI writer, optionally interleaved with stereo PCM16.
// JPEG encoding, audio mixing and pixel/color admission retain their owners.
class AviMovie {
  public:
    AviMovie(unsigned width, unsigned height, MovieFrameRate, unsigned frames,
             std::size_t byte_limit = avi_movie_byte_limit);
    AviMovie(unsigned width, unsigned height, MovieFrameRate, unsigned frames, MovieAudio,
             std::size_t byte_limit = avi_movie_byte_limit);
    void append(std::span<const std::byte> jpeg);
    // Exactly the samples between the next two video boundaries, interleaved L/R.
    void append(std::span<const std::byte> jpeg, std::span<const std::int16_t> stereo);
    std::vector<std::byte> finish();

  private:
    std::vector<std::byte> bytes_;
    struct IndexEntry {
        std::uint32_t offset, size;
        bool audio;
    };
    std::vector<IndexEntry> index_;
    std::size_t limit_ = 0, movi_size_ = 0, movi_type_ = 0;
    std::size_t main_buffer_ = 0, stream_buffer_ = 0, audio_buffer_ = 0;
    unsigned frames_ = 0, appended_ = 0;
    std::uint32_t largest_ = 0, largest_audio_ = 0;
    MovieFrameRate rate_;
    MovieAudio audio_ = MovieAudio::None;
    bool finished_ = false;
    void word(std::uint16_t);
    void integer(std::uint32_t);
    void fourcc(const char*);
    void patch(std::size_t, std::uint32_t);
};
} // namespace forge::asset_detail
