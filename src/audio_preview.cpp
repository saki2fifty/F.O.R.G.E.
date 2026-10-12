#include "audio_preview.hpp"
#include "audio_preview_access.hpp"
#include "audio_selection.hpp"
#include <algorithm>
#include <cmath>
#include <forge/scene.hpp>
namespace forge {
namespace asset_detail {
AudioWaveform audio_waveform(const AudioClipData& clip) {
    validate_audio_clip(clip);
    AudioWaveform result{{}, clip.frames, clip.channels, clip.rate};
    const auto count = std::min<std::uint64_t>(4096, clip.frames);
    result.peaks.reserve(count * clip.channels);
    for (std::uint64_t bucket = 0; bucket < count; ++bucket) {
        const auto begin = bucket * clip.frames / count;
        const auto end = (bucket + 1) * clip.frames / count;
        for (unsigned channel = 0; channel < clip.channels; ++channel) {
            float minimum = clip.pcm[begin * clip.channels + channel], maximum = minimum;
            for (auto frame = begin + 1; frame < end; ++frame) {
                const auto sample = clip.pcm[frame * clip.channels + channel];
                minimum = std::min(minimum, sample);
                maximum = std::max(maximum, sample);
            }
            result.peaks.push_back({minimum, maximum});
        }
    }
    return result;
}
} // namespace asset_detail
namespace {
bool same_revision(const AssetRecord& a, const AssetRecord& b) {
    return a.id == b.id && a.type == b.type && a.source == b.source &&
           a.schema_version == b.schema_version && a.metadata == b.metadata;
}
AssetRecord selected(const std::filesystem::path& project, const AssetRecord& expected) {
    const auto catalog = AssetCatalog::open_project(project);
    const auto found = catalog.records().find(expected.id);
    if (found == catalog.records().end() || expected.type != AudioClipAsset::type ||
        expected.schema_version != 1 || !same_revision(expected, found->second))
        throw std::runtime_error("Audio preview selection changed. Refresh or reimport the clip.");
    // Published cooked clips remain playable when their original WAV is unavailable.
    // The normal audio loader owns artifact/digest/PCM admission below.
    return found->second;
}
} // namespace
struct AudioClipPreview::PreviewWorld {
    std::filesystem::path project;
    AssetRecord record;
    EngineContext engine;
    Scene scene;
    std::shared_ptr<AudioRuntime> audio;
    std::string actor = AssetId::generate().str();
    bool paused = false;
    double seek_offset = 0;
    EntityRef ref() const { return scene.reference(actor); }
    PreviewWorld(const std::filesystem::path& root, const AssetRecord& expected, AudioOutput output,
                 bool loop, float volume)
        : project(root), record(selected(root, expected)),
          engine(WorldRole::Runtime, false, {audio_module({root, output, true, volume})}),
          scene(engine.world()),
          audio(std::static_pointer_cast<AudioRuntime>(engine.services().audio())) {
        scene.restore_snapshot(
            {{"version", 3},
             {"asset_id", AssetId::generate()},
             {"entities", Json::array({{{"id", actor},
                                        {"name", "Clip preview"},
                                        {"components",
                                         {{"forge.audio_source",
                                           {{"clip", record.id},
                                            {"play_on_start", true},
                                            {"loop", loop},
                                            {"gain", 1.f},
                                            {"pitch", 1.f},
                                            {"spatialized", false},
                                            {"minimum_distance", 1.f},
                                            {"maximum_distance", 100.f}}}}}}})}});
        audio->synchronize(0);
        if (audio->status().at("voices") != 1) {
            const auto diagnostics = engine.services().diagnostics();
            throw std::runtime_error(diagnostics.empty()
                                         ? "Audio preview could not load this clip. Reimport it."
                                         : diagnostics.back().at("text").get<std::string>());
        }
        audio->paused(false);
    }
};
AudioClipPreview::AudioClipPreview() = default;
AudioClipPreview::~AudioClipPreview() = default;
void AudioClipPreview::start(const std::filesystem::path& project, const AssetRecord& record,
                             AudioOutput output) {
    stop();
    world_ = std::make_unique<PreviewWorld>(project, record, output, loop_, volume_);
    try {
        if (!waveform_matches(project, record)) {
            waveform_ = asset_detail::audio_waveform(
                AudioPreviewAccess::clip(*world_->audio, world_->ref()));
            waveform_project_ = project;
            waveform_record_ = record;
        }
    } catch (...) {
        stop();
        throw;
    }
}
void AudioClipPreview::stop() { world_.reset(); }
void AudioClipPreview::clear() {
    stop();
    waveform_ = {};
    waveform_project_.clear();
    waveform_record_.reset();
}
void AudioClipPreview::load_waveform(const std::filesystem::path& project,
                                     const AssetRecord& record) {
    const auto admitted = selected(project, record);
    if (waveform_matches(project, admitted))
        return;
    auto envelope = asset_detail::audio_waveform(asset_detail::load_audio_clip(project, admitted));
    waveform_ = std::move(envelope);
    waveform_project_ = project;
    waveform_record_ = admitted;
}
const asset_detail::AudioWaveform* AudioClipPreview::waveform() const {
    return waveform_record_ ? &waveform_ : nullptr;
}
bool AudioClipPreview::waveform_matches(const std::filesystem::path& project,
                                        const AssetRecord& record) const {
    return waveform_record_ && waveform_project_ == project &&
           same_revision(*waveform_record_, record);
}
void AudioClipPreview::paused(bool value) {
    if (!world_)
        throw std::runtime_error("Audio preview is stopped");
    world_->audio->paused(value);
    world_->paused = value;
}
bool AudioClipPreview::paused() const { return world_ && world_->paused; }
void AudioClipPreview::seek(double seconds) {
    if (!world_)
        throw std::runtime_error("Audio preview is stopped");
    if (!std::isfinite(seconds) || seconds < 0 || seconds > waveform_.duration())
        throw std::runtime_error("Preview position must be within the clip duration");
    try {
        world_->seek_offset = AudioPreviewAccess::seek(*world_->audio, world_->ref(), seconds);
    } catch (...) {
        stop();
        throw;
    }
}
double AudioClipPreview::position() const {
    if (!world_)
        return 0;
    const auto duration = waveform_.duration();
    const double seconds =
        world_->seek_offset + AudioPreviewAccess::elapsed(*world_->audio, world_->ref());
    return loop_ ? std::fmod(seconds, duration) : std::min(seconds, duration);
}
void AudioClipPreview::configure(bool loop, float volume) {
    if (!std::isfinite(volume) || volume < 0 || volume > 1)
        throw std::runtime_error("Preview volume must be finite and between zero and one");
    if (world_ && loop != loop_) {
        auto entity = world_->scene.entity(world_->actor);
        auto source = entity.get<AudioSource>();
        source.loop = loop;
        entity.set(source);
        world_->audio->synchronize(0);
    }
    if (world_ && volume != volume_)
        world_->audio->master_volume(volume);
    loop_ = loop;
    volume_ = volume;
}
void AudioClipPreview::poll() {
    if (world_ && world_->audio->status().at("playing_sources") == 0)
        stop();
}
bool AudioClipPreview::matches(const std::filesystem::path& project,
                               const AssetRecord& record) const {
    return world_ && world_->project == project && same_revision(world_->record, record);
}
void AudioClipPreview::read_offline(std::span<float> output) {
    if (!world_)
        throw std::runtime_error("Audio preview is stopped");
    world_->audio->read_offline(output);
}
} // namespace forge
