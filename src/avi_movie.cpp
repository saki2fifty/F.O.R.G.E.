#include "avi_movie.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace forge::asset_detail {
namespace {
void require(bool value, const char* reason) {
    if (!value)
        throw std::runtime_error(reason);
}
constexpr std::uint64_t denominator_limit = 10000000;
// Keep rates representable by readers using signed 32-bit rational components.
constexpr std::uint64_t integer_limit = std::numeric_limits<std::int32_t>::max();
void validate(MovieFrameRate fps) {
    require(fps.rate && fps.rate <= integer_limit && fps.scale && fps.scale <= denominator_limit &&
                std::gcd(fps.rate, fps.scale) == 1 && std::uint64_t(fps.rate) * 1000 >= fps.scale &&
                fps.rate <= std::uint64_t(fps.scale) * 240,
            "Movie FPS must be a reduced rational from 0.001 to 240");
}
} // namespace
MovieFrameRate avi_movie_frame_rate(double fps) {
    require(std::isfinite(fps) && fps >= .001 && fps <= 240,
            "Movie FPS must be finite from 0.001 to 240");
    std::uint64_t p0 = 0, p1 = 1, q0 = 1, q1 = 0;
    double remainder = fps;
    MovieFrameRate best;
    double error = std::numeric_limits<double>::infinity();
    for (unsigned step = 0; step < 64; ++step) {
        const auto term = std::floor(remainder);
        const auto maximum = std::min(p1 ? (integer_limit - p0) / p1 : integer_limit,
                                      q1 ? (denominator_limit - q0) / q1 : integer_limit);
        const bool bounded = !std::isfinite(term) || term > double(maximum);
        const auto a = bounded ? maximum : std::uint64_t(term);
        const auto p = p0 + a * p1, q = q0 + a * q1;
        if (p && q) {
            const auto candidate_error = std::abs(double(p) / double(q) - fps);
            if (candidate_error < error) {
                const auto divisor = std::gcd(p, q);
                best = {std::uint32_t(p / divisor), std::uint32_t(q / divisor)};
                error = candidate_error;
            }
        }
        if (bounded || error <= std::max(1e-12, fps * 1e-12))
            break;
        const auto fraction = remainder - term;
        if (fraction <= 0)
            break;
        p0 = p1;
        p1 = p;
        q0 = q1;
        q1 = q;
        remainder = 1 / fraction;
    }
    require(error <= std::max(1e-10, fps * 1e-10),
            "Movie FPS cannot be represented within the bounded timebase precision");
    validate(best);
    return best;
}
void AviMovie::word(std::uint16_t value) {
    bytes_.push_back(std::byte(value & 255));
    bytes_.push_back(std::byte(value >> 8));
}
void AviMovie::integer(std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8)
        bytes_.push_back(std::byte((value >> shift) & 255));
}
void AviMovie::fourcc(const char* value) {
    for (unsigned i = 0; i < 4; ++i)
        bytes_.push_back(std::byte(value[i]));
}
void AviMovie::patch(std::size_t at, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8)
        bytes_.at(at++) = std::byte((value >> shift) & 255);
}
AviMovie::AviMovie(unsigned width, unsigned height, MovieFrameRate fps, unsigned frames,
                   std::size_t limit)
    : limit_(limit), frames_(frames) {
    validate(fps);
    require(width && height && width <= FORGE_VIEW_IMAGE_MAX_EXTENT &&
                height <= FORGE_VIEW_IMAGE_MAX_EXTENT,
            "Movie axes must be between 1 and 4096");
    require(frames && frames <= FORGE_SEQUENCE_FRAME_MAX_COUNT, "Movie requires 1 to 4096 frames");
    require(limit <= avi_movie_byte_limit && limit >= 232 + std::size_t(frames) * 28,
            "Movie byte budget cannot contain its bounded headers and index");
    bytes_.reserve(232 + std::size_t(frames) * 28);
    index_.reserve(frames);
    fourcc("RIFF");
    integer(0);
    fourcc("AVI ");
    fourcc("LIST");
    integer(192);
    fourcc("hdrl");
    fourcc("avih");
    integer(56);
    integer(std::uint32_t(std::llround(1000000. * fps.scale / fps.rate)));
    integer(0); // Data-rate estimate unspecified; no measured playback-rate claim.
    integer(0);
    integer(0x10); // Complete idx1 is published with the file.
    integer(frames);
    integer(0);
    integer(1);
    main_buffer_ = bytes_.size();
    integer(0);
    integer(width);
    integer(height);
    for (unsigned i = 0; i < 4; ++i)
        integer(0);
    fourcc("LIST");
    integer(116);
    fourcc("strl");
    fourcc("strh");
    integer(56);
    fourcc("vids");
    fourcc("MJPG");
    integer(0);
    word(0);
    word(0);
    integer(0);
    integer(fps.scale);
    integer(fps.rate);
    integer(0);
    integer(frames);
    stream_buffer_ = bytes_.size();
    integer(0);
    integer(0xffffffffu);
    integer(0);
    word(0);
    word(0);
    word(std::uint16_t(width));
    word(std::uint16_t(height));
    fourcc("strf");
    integer(40);
    integer(40);
    integer(width);
    integer(height);
    word(1);
    word(24);
    fourcc("MJPG");
    integer(width * height * 3);
    for (unsigned i = 0; i < 4; ++i)
        integer(0);
    fourcc("LIST");
    movi_size_ = bytes_.size();
    integer(0);
    movi_type_ = bytes_.size();
    fourcc("movi");
    require(bytes_.size() == 224, "Movie header layout is inconsistent");
}
void AviMovie::append(std::span<const std::byte> jpeg) {
    require(!finished_ && index_.size() < frames_, "Movie has no remaining frame slot");
    require(jpeg.size() >= 4 && jpeg.size() <= FORGE_VIEW_IMAGE_ENCODED_LIMIT &&
                jpeg[0] == std::byte{255} && jpeg[1] == std::byte{216} &&
                jpeg[jpeg.size() - 2] == std::byte{255} && jpeg.back() == std::byte{217},
            "Movie requires a complete encoded JPEG frame");
    const auto extent = 8 + jpeg.size() + (jpeg.size() & 1);
    const auto tail = 8 + std::size_t(frames_) * 16;
    require(extent <= limit_ && tail <= limit_ - extent && bytes_.size() <= limit_ - extent - tail,
            "Movie exceeds its encoded byte budget");
    const auto needed = bytes_.size() + extent + tail;
    if (needed > bytes_.capacity())
        bytes_.reserve(std::max(needed, std::min(limit_, bytes_.capacity() * 2)));
    index_.emplace_back(std::uint32_t(bytes_.size() - movi_type_), std::uint32_t(jpeg.size()));
    fourcc("00dc");
    integer(std::uint32_t(jpeg.size()));
    bytes_.insert(bytes_.end(), jpeg.begin(), jpeg.end());
    if (jpeg.size() & 1)
        bytes_.push_back(std::byte{});
    largest_ = std::max(largest_, std::uint32_t(jpeg.size()));
}
std::vector<std::byte> AviMovie::finish() {
    require(!finished_ && index_.size() == frames_, "Movie does not have its planned frames");
    patch(movi_size_, std::uint32_t(bytes_.size() - movi_type_));
    fourcc("idx1");
    integer(std::uint32_t(index_.size() * 16));
    for (const auto& [offset, size] : index_) {
        fourcc("00dc");
        integer(0x10); // Every independently encoded JPEG frame is a keyframe.
        integer(offset);
        integer(size);
    }
    patch(4, std::uint32_t(bytes_.size() - 8));
    patch(main_buffer_, largest_);
    patch(stream_buffer_, largest_);
    finished_ = true;
    return std::move(bytes_);
}
} // namespace forge::asset_detail
