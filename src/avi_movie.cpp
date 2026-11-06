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
constexpr auto denominator_limit = movie_rate_scale_limit;
// Keep rates representable by readers using signed 32-bit rational components.
constexpr auto integer_limit = movie_rate_integer_limit;
void validate(MovieFrameRate fps) {
    require(valid_movie_frame_rate(fps), "Movie FPS must be a reduced rational from 0.001 to 240");
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
    : AviMovie(width, height, fps, frames, MovieAudio::None, limit) {}
AviMovie::AviMovie(unsigned width, unsigned height, MovieFrameRate fps, unsigned frames,
                   MovieAudio audio, std::size_t limit)
    : limit_(limit), frames_(frames), rate_(fps), audio_(audio) {
    validate(fps);
    require(width && height && width <= FORGE_VIEW_IMAGE_MAX_EXTENT &&
                height <= FORGE_VIEW_IMAGE_MAX_EXTENT,
            "Movie axes must be between 1 and 4096");
    require(frames && frames <= FORGE_SEQUENCE_FRAME_MAX_COUNT, "Movie requires 1 to 4096 frames");
    require(audio == MovieAudio::None || audio == MovieAudio::PcmStereo48k,
            "Unsupported movie audio profile");
    const bool sound = audio == MovieAudio::PcmStereo48k;
    const auto audio_frames = sound ? avi_movie_audio_frames(fps, frames) : 0;
    const auto minimum = 232 + std::size_t(frames) * 28 +
                         (sound ? 100 + audio_frames * 4 + std::size_t(frames) * 24 : 0);
    require(limit <= avi_movie_byte_limit && minimum <= limit,
            "Movie byte budget cannot contain its bounded headers and index");
    bytes_.reserve(232 + (sound ? 100 : 0) + std::size_t(frames) * (sound ? 52 : 28));
    index_.reserve(std::size_t(frames) * (sound ? 2 : 1));
    fourcc("RIFF");
    integer(0);
    fourcc("AVI ");
    fourcc("LIST");
    integer(sound ? 292 : 192);
    fourcc("hdrl");
    fourcc("avih");
    integer(56);
    integer(std::uint32_t(std::llround(1000000. * fps.scale / fps.rate)));
    integer(0); // Data-rate estimate unspecified; no measured playback-rate claim.
    integer(0);
    integer(sound ? 0x110 : 0x10); // Complete index; audio packets interleave with video.
    integer(frames);
    integer(0);
    integer(sound ? 2 : 1);
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
    if (sound) {
        fourcc("LIST");
        integer(92);
        fourcc("strl");
        fourcc("strh");
        integer(56);
        fourcc("auds");
        integer(0); // PCM has no codec handler.
        integer(0);
        word(0);
        word(0);
        integer(0);
        integer(4); // One stereo PCM16 frame is one four-byte audio block.
        integer(movie_audio_sample_rate * 4);
        integer(0);
        integer(std::uint32_t(audio_frames));
        audio_buffer_ = bytes_.size();
        integer(0);
        integer(0xffffffffu);
        integer(4);
        for (unsigned i = 0; i < 4; ++i)
            word(0);
        fourcc("strf");
        integer(16); // PCMWAVEFORMAT; PCM needs no extra format bytes.
        word(1);
        word(2);
        integer(movie_audio_sample_rate);
        integer(movie_audio_sample_rate * 4);
        word(4);
        word(16);
    }
    fourcc("LIST");
    movi_size_ = bytes_.size();
    integer(0);
    movi_type_ = bytes_.size();
    fourcc("movi");
    require(bytes_.size() == (sound ? 324 : 224), "Movie header layout is inconsistent");
}
void AviMovie::append(std::span<const std::byte> jpeg) { append(jpeg, {}); }
void AviMovie::append(std::span<const std::byte> jpeg, std::span<const std::int16_t> stereo) {
    require(!finished_ && appended_ < frames_, "Movie has no remaining frame slot");
    const bool sound = audio_ == MovieAudio::PcmStereo48k;
    const auto samples = sound ? (avi_movie_audio_frames(rate_, appended_ + 1) -
                                  avi_movie_audio_frames(rate_, appended_)) *
                                     2
                               : 0;
    require(stereo.size() == samples, "Movie PCM must match its exact video interval");
    require(jpeg.size() >= 4 && jpeg.size() <= FORGE_VIEW_IMAGE_ENCODED_LIMIT &&
                jpeg[0] == std::byte{255} && jpeg[1] == std::byte{216} &&
                jpeg[jpeg.size() - 2] == std::byte{255} && jpeg.back() == std::byte{217},
            "Movie requires a complete encoded JPEG frame");
    const auto audio_bytes = stereo.size() * 2;
    const auto extent = 8 + jpeg.size() + (jpeg.size() & 1) + (sound ? 8 + audio_bytes : 0);
    const auto tail = 8 + std::size_t(frames_) * (sound ? 32 : 16);
    require(extent <= limit_ && tail <= limit_ - extent && bytes_.size() <= limit_ - extent - tail,
            "Movie exceeds its encoded byte budget");
    const auto needed = bytes_.size() + extent + tail;
    if (needed > bytes_.capacity())
        bytes_.reserve(std::max(needed, std::min(limit_, bytes_.capacity() * 2)));
    index_.push_back(
        {std::uint32_t(bytes_.size() - movi_type_), std::uint32_t(jpeg.size()), false});
    fourcc("00dc");
    integer(std::uint32_t(jpeg.size()));
    bytes_.insert(bytes_.end(), jpeg.begin(), jpeg.end());
    if (jpeg.size() & 1)
        bytes_.push_back(std::byte{});
    largest_ = std::max(largest_, std::uint32_t(jpeg.size()));
    if (sound) {
        index_.push_back(
            {std::uint32_t(bytes_.size() - movi_type_), std::uint32_t(audio_bytes), true});
        fourcc("01wb");
        integer(std::uint32_t(audio_bytes));
        for (const auto sample : stereo)
            word(std::uint16_t(sample));
        largest_audio_ = std::max(largest_audio_, std::uint32_t(audio_bytes));
    }
    ++appended_;
}
std::vector<std::byte> AviMovie::finish() {
    require(!finished_ && appended_ == frames_, "Movie does not have its planned frames");
    patch(movi_size_, std::uint32_t(bytes_.size() - movi_type_));
    fourcc("idx1");
    integer(std::uint32_t(index_.size() * 16));
    for (const auto& [offset, size, audio] : index_) {
        fourcc(audio ? "01wb" : "00dc");
        integer(0x10); // Every independently encoded JPEG frame is a keyframe.
        integer(offset);
        integer(size);
    }
    patch(4, std::uint32_t(bytes_.size() - 8));
    patch(main_buffer_, std::max(largest_, largest_audio_));
    patch(stream_buffer_, largest_);
    if (audio_ == MovieAudio::PcmStereo48k)
        patch(audio_buffer_, largest_audio_);
    finished_ = true;
    return std::move(bytes_);
}
} // namespace forge::asset_detail
