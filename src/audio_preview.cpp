#include "audio_preview.hpp"
#include <cmath>
#include <forge/scene.hpp>
namespace forge {
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
}
void AudioClipPreview::stop() { world_.reset(); }
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
