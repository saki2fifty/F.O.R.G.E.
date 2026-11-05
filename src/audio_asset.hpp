#pragma once
#include <cstdint>
#include <span>
#include <stop_token>
#include <vector>
namespace forge::asset_detail {
inline constexpr std::size_t max_audio_source_bytes = 16 * 1024 * 1024;
inline constexpr std::size_t max_audio_pcm_bytes = 64 * 1024 * 1024;
inline constexpr std::size_t audio_pcm_header_bytes = 32;
// Exact pinned miniaudio WAV admission: MA_DR_WAV_MAX_SAMPLE_RATE.
inline constexpr std::uint32_t max_audio_sample_rate = 384000;
inline constexpr std::uint32_t audio_stream_sample_rate = 48000;
struct AudioClipInfo {
    std::uint64_t frames = 0;
    std::uint32_t channels = 0, rate = 0;
    double duration() const { return rate ? double(frames) / rate : 0; }
};
struct AudioClipData {
    std::vector<float> pcm;
    std::uint64_t frames = 0;
    std::uint32_t channels = 0, rate = 0;
    double duration() const { return rate ? double(frames) / rate : 0; }
};
enum class AudioSourceFormat { Wav, Flac, Mp3 };
// Native miniaudio decode, shared by the isolated cooker and legacy runtime
// source path. No device, engine, voices, ECS or editor objects are created.
AudioClipData decode_audio_source(std::span<const std::byte>, AudioSourceFormat);
AudioClipData decode_audio_wav(std::span<const std::byte>);
// Streaming output has a stable frame clock before native page/loop realization.
// Existing resident imports retain their original decoded rate and samples.
void prepare_streaming_audio(AudioClipData&, std::stop_token = {});
// Private cooked PCM transport: exact header/length/finite-sample admission.
// It does not contain a native miniaudio object or platform-endian struct dump.
void validate_audio_clip(const AudioClipData&);
void validate_audio_info(const AudioClipInfo&);
AudioClipInfo validate_audio_clip_bytes(std::span<const std::byte>);
std::vector<std::byte> encode_audio_clip(const AudioClipData&);
AudioClipData decode_audio_clip(std::span<const std::byte>);
} // namespace forge::asset_detail
