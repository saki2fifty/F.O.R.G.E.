#pragma once
#include "view_image_limits.h"
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace forge::asset_detail {
inline constexpr std::size_t avi_movie_byte_limit = 256ull * 1024 * 1024;
struct MovieFrameRate {
    std::uint32_t rate = 0, scale = 0;
    bool operator==(const MovieFrameRate&) const = default;
};
// Container timing only. This does not change the source timeline's sample times.
// A bounded rational retains fractional rates without per-frame time rounding.
MovieFrameRate avi_movie_frame_rate(double frames_per_second);

// Private, bounded single-stream MJPEG AVI writer. JPEG encoding and pixel/color
// admission stay with the existing image owner. No platform structs or codecs.
class AviMovie {
  public:
    AviMovie(unsigned width, unsigned height, MovieFrameRate, unsigned frames,
             std::size_t byte_limit = avi_movie_byte_limit);
    void append(std::span<const std::byte> jpeg);
    std::vector<std::byte> finish();

  private:
    std::vector<std::byte> bytes_;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> index_;
    std::size_t limit_ = 0, movi_size_ = 0, movi_type_ = 0;
    std::size_t main_buffer_ = 0, stream_buffer_ = 0;
    unsigned frames_ = 0;
    std::uint32_t largest_ = 0;
    bool finished_ = false;
    void word(std::uint16_t);
    void integer(std::uint32_t);
    void fourcc(const char*);
    void patch(std::size_t, std::uint32_t);
};
} // namespace forge::asset_detail
