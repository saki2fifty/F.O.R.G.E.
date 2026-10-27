#include "audio_asset.hpp"
#include <cmath>
#include <miniaudio.h>
#include <stdexcept>
#include <string>
namespace forge::asset_detail {
AudioClipData decode_audio_source(std::span<const std::byte> bytes, AudioSourceFormat format) {
    if (bytes.size() < 12 || bytes.size() > max_audio_source_bytes)
        throw std::runtime_error("Audio source must be 12 bytes to 16 MiB");
    auto checked = [](ma_result result, const char* action) {
        if (result != MA_SUCCESS)
            throw std::runtime_error(std::string(action) + ": " + ma_result_description(result));
    };
    ma_decoder decoder{};
    auto configuration = ma_decoder_config_init(ma_format_f32, 0, 0);
    switch (format) {
    case AudioSourceFormat::Wav:
        configuration.encodingFormat = ma_encoding_format_wav;
        break;
    case AudioSourceFormat::Flac:
        configuration.encodingFormat = ma_encoding_format_flac;
        break;
    case AudioSourceFormat::Mp3:
        configuration.encodingFormat = ma_encoding_format_mp3;
        break;
    default:
        throw std::runtime_error("Unsupported audio source format");
    }
    checked(ma_decoder_init_memory(bytes.data(), bytes.size(), &configuration, &decoder),
            "Audio decode");
    struct End {
        ma_decoder* decoder;
        ~End() { ma_decoder_uninit(decoder); }
    } end{&decoder};
    AudioClipData clip;
    clip.channels = decoder.outputChannels;
    clip.rate = decoder.outputSampleRate;
    ma_uint64 frames = 0;
    checked(ma_decoder_get_length_in_pcm_frames(&decoder, &frames), "Audio length");
    clip.frames = frames;
    if (!clip.frames || !clip.rate || clip.channels < 1 || clip.channels > 2 ||
        clip.frames > max_audio_pcm_bytes / (sizeof(float) * clip.channels))
        throw std::runtime_error("Audio must be mono/stereo and decode to at most 64 MiB");
    clip.pcm.resize(std::size_t(clip.frames * clip.channels));
    ma_uint64 read = 0;
    checked(ma_decoder_read_pcm_frames(&decoder, clip.pcm.data(), frames, &read), "Audio samples");
    if (read != frames)
        throw std::runtime_error("Truncated audio samples");
    validate_audio_clip(clip);
    return clip;
}
AudioClipData decode_audio_wav(std::span<const std::byte> bytes) {
    return decode_audio_source(bytes, AudioSourceFormat::Wav);
}
} // namespace forge::asset_detail
