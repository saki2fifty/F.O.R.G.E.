#include "asset_bytes.hpp"
#include "audio_bundle.hpp"
#include "audio_importer.hpp"
#include <algorithm>
#include <cmath>
namespace forge::asset_detail {
namespace {
void process(AudioClipData& clip, const nlohmann::json& settings, std::stop_token stop) {
    auto cancelled = [&](std::uint64_t at) {
        if (at % 4096 == 0 && stop.stop_requested())
            throw std::runtime_error("Audio processing cancelled");
    };
    if (settings.at("force_mono") == true && clip.channels == 2) {
        for (std::uint64_t frame = 0; frame < clip.frames; ++frame) {
            cancelled(frame);
            // Double intermediate avoids overflow for finite float WAV samples.
            clip.pcm[frame] = float((double(clip.pcm[frame * 2]) + clip.pcm[frame * 2 + 1]) * .5);
        }
        clip.channels = 1;
        clip.pcm.resize(clip.frames);
    }
    if (settings.at("normalize_peak") == true) {
        double peak = 0;
        for (std::size_t i = 0; i < clip.pcm.size(); ++i) {
            cancelled(i);
            peak = std::max(peak, std::abs(double(clip.pcm[i])));
        }
        if (peak > 0)
            for (std::size_t i = 0; i < clip.pcm.size(); ++i) {
                cancelled(i);
                clip.pcm[i] = float(double(clip.pcm[i]) / peak);
            }
    }
    if (settings.at("trim_silence") == true) {
        const double threshold =
            std::pow(10., settings.at("trim_threshold_db").get<double>() / 20.);
        auto audible = [&](std::uint64_t frame) {
            cancelled(frame);
            for (unsigned channel = 0; channel < clip.channels; ++channel)
                if (std::abs(double(clip.pcm[frame * clip.channels + channel])) > threshold)
                    return true;
            return false;
        };
        std::uint64_t first = 0, end = clip.frames;
        while (first < end && !audible(first))
            ++first;
        while (end > first && !audible(end - 1))
            --end;
        if (first == end) {
            clip.frames = 1;
            clip.pcm.assign(clip.channels, 0);
        } else {
            const bool fade_start = first != 0, fade_end = end != clip.frames;
            if (first != 0)
                std::copy(clip.pcm.begin() + first * clip.channels,
                          clip.pcm.begin() + end * clip.channels, clip.pcm.begin());
            clip.frames = end - first;
            clip.pcm.resize(clip.frames * clip.channels);
            const auto fade =
                std::min<std::uint64_t>(std::max(1u, clip.rate / 200), clip.frames / 2);
            if (fade >= 2)
                for (std::uint64_t frame = 0; frame < fade; ++frame) {
                    cancelled(frame);
                    const float gain = float(double(frame) / (fade - 1));
                    for (unsigned channel = 0; channel < clip.channels; ++channel) {
                        if (fade_start)
                            clip.pcm[frame * clip.channels + channel] *= gain;
                        if (fade_end)
                            clip.pcm[(clip.frames - 1 - frame) * clip.channels + channel] *= gain;
                    }
                }
        }
    }
    if (stop.stop_requested())
        throw std::runtime_error("Audio processing cancelled");
    validate_audio_clip(clip);
}
} // namespace
std::vector<ArtifactFile> execute_audio_recipe(const ImportProcessRequest& request,
                                               std::stop_token stop) {
    const auto& p = request.payload;
    if (stop.stop_requested())
        throw std::runtime_error("Audio import cancelled");
    if (p.at("recipe") != "forge.audio.wav" ||
        p.at("revision").get<std::string>() != audio_recipe_revision() ||
        request.inputs.size() != 1 || request.inputs.front().name != "source.wav")
        throw std::runtime_error("Invalid audio worker recipe/revision/input");
    const auto settings =
        audio_settings().effective(p.at("settings").get<ImportSettingsDocument>());
    const auto& source = request.inputs.front().bytes;
    const auto digest = content_digest(source);
    if (digest != p.at("source_digest").get<std::string>())
        throw std::runtime_error("Audio source snapshot digest changed");
    auto decoded = decode_audio_wav(source);
    if (stop.stop_requested())
        throw std::runtime_error("Audio import cancelled after decode");
    process(decoded, settings, stop);
    const auto text = audio_clip_metadata(decoded, digest).dump();
    const auto span = std::as_bytes(std::span(text));
    return {{"audio.fpcm", encode_audio_clip(decoded)}, {"audio.json", {span.begin(), span.end()}}};
}
} // namespace forge::asset_detail
