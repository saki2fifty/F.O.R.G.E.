#include "audio_asset.hpp"
#include <algorithm>
#include <miniaudio.h>
#include <stdexcept>

namespace forge::asset_detail {
void prepare_streaming_audio(AudioClipData& clip, std::stop_token stop) {
    validate_audio_clip(clip);
    if (stop.stop_requested())
        throw std::runtime_error("Audio streaming preparation cancelled");
    if (clip.rate != audio_stream_sample_rate) {
        const auto frames = (clip.frames * audio_stream_sample_rate + clip.rate - 1) / clip.rate;
        if (!frames || frames > max_audio_pcm_bytes / (sizeof(float) * clip.channels))
            throw std::runtime_error("Streamed audio at 48000 Hz exceeds the 64 MiB PCM limit");
        ma_resampler resampler{};
        const auto config =
            ma_resampler_config_init(ma_format_f32, clip.channels, clip.rate,
                                     audio_stream_sample_rate, ma_resample_algorithm_linear);
        if (ma_resampler_init(&config, nullptr, &resampler) != MA_SUCCESS)
            throw std::runtime_error("Audio streaming resampler could not be initialized");
        struct Release {
            ma_resampler* value;
            ~Release() { ma_resampler_uninit(value, nullptr); }
        } release{&resampler};
        std::vector<float> converted(std::size_t(frames * clip.channels));
        ma_uint64 input = 0, output = 0;
        while (output < frames) {
            if (stop.stop_requested())
                throw std::runtime_error("Audio streaming preparation cancelled");
            const bool padding = input == clip.frames;
            ma_uint64 in_count = padding ? 4096 : std::min<ma_uint64>(4096, clip.frames - input);
            ma_uint64 out_count = std::min<ma_uint64>(4096, frames - output);
            const auto result = ma_resampler_process_pcm_frames(
                &resampler, padding ? nullptr : clip.pcm.data() + input * clip.channels, &in_count,
                converted.data() + output * clip.channels, &out_count);
            if (result != MA_SUCCESS || (!in_count && !out_count))
                throw std::runtime_error("Audio streaming resampler made no progress");
            if (!padding)
                input += in_count;
            output += out_count;
        }
        clip.pcm = std::move(converted);
        clip.frames = frames;
        clip.rate = audio_stream_sample_rate;
    }
    if (clip.frames < audio_stream_sample_rate)
        throw std::runtime_error(
            "Streamed audio requires at least one second; use resident playback for shorter clips");
    validate_audio_clip(clip);
}
} // namespace forge::asset_detail
