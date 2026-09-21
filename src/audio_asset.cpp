#include "audio_asset.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace forge::asset_detail {
namespace {
constexpr std::size_t header_bytes = 32;
constexpr char signature[8] = {'F', 'O', 'R', 'G', 'E', 'A', 'U', 'D'};
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
void put(std::span<std::byte> out, std::size_t at, std::uint64_t value, unsigned count) {
    for (unsigned i = 0; i < count; ++i)
        out[at + i] = std::byte(value >> (i * 8));
}
std::uint64_t get(std::span<const std::byte> bytes, std::size_t at, unsigned count) {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < count; ++i)
        value |= std::uint64_t(std::to_integer<unsigned>(bytes[at + i])) << (i * 8);
    return value;
}
std::size_t sample_count(std::uint64_t frames, unsigned channels, unsigned rate) {
    require(frames && rate && rate <= max_audio_sample_rate && channels >= 1 && channels <= 2,
            "Audio requires nonempty mono/stereo PCM and a sample rate of 1..384000 Hz");
    require(frames <= max_audio_pcm_bytes / (sizeof(float) * channels),
            "Decoded AudioClip exceeds 64 MiB");
    return std::size_t(frames * channels);
}
} // namespace
void validate_audio_clip(const AudioClipData& clip) {
    require(clip.pcm.size() == sample_count(clip.frames, clip.channels, clip.rate),
            "AudioClip frame/channel count disagrees with PCM length");
    for (float value : clip.pcm)
        require(std::isfinite(value), "Nonfinite audio sample");
}
std::vector<std::byte> encode_audio_clip(const AudioClipData& clip) {
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
    validate_audio_clip(clip);
    std::vector<std::byte> bytes(header_bytes + clip.pcm.size() * sizeof(float));
    for (unsigned i = 0; i < 8; ++i)
        bytes[i] = std::byte(signature[i]);
    put(bytes, 8, 1, 4);
    put(bytes, 12, clip.channels, 4);
    put(bytes, 16, clip.rate, 4);
    put(bytes, 20, 0, 4); // Reserved: no flags or implicit encoding variants.
    put(bytes, 24, clip.frames, 8);
    for (std::size_t i = 0; i < clip.pcm.size(); ++i)
        put(bytes, header_bytes + i * 4, std::bit_cast<std::uint32_t>(clip.pcm[i]), 4);
    return bytes;
}
AudioClipData decode_audio_clip(std::span<const std::byte> bytes) {
    require(bytes.size() >= header_bytes && bytes.size() <= header_bytes + max_audio_pcm_bytes,
            "Cooked AudioClip is truncated or exceeds its byte limit");
    for (unsigned i = 0; i < 8; ++i)
        require(bytes[i] == std::byte(signature[i]), "Invalid cooked AudioClip signature");
    require(get(bytes, 8, 4) == 1 && get(bytes, 20, 4) == 0,
            "Unsupported cooked AudioClip version or flags");
    AudioClipData clip;
    clip.channels = unsigned(get(bytes, 12, 4));
    clip.rate = unsigned(get(bytes, 16, 4));
    clip.frames = get(bytes, 24, 8);
    const auto count = sample_count(clip.frames, clip.channels, clip.rate);
    require(bytes.size() == header_bytes + count * sizeof(float),
            "Cooked AudioClip length does not match its frames");
    clip.pcm.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        clip.pcm[i] = std::bit_cast<float>(std::uint32_t(get(bytes, header_bytes + i * 4, 4)));
        require(std::isfinite(clip.pcm[i]), "Nonfinite cooked AudioClip sample");
    }
    return clip;
}
} // namespace forge::asset_detail
