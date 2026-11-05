#include "audio_selection.hpp"
#include "asset_bytes.hpp"
#include "audio_bundle.hpp"
#include "audio_importer.hpp"
#include "audio_resource_data.hpp"
#include <forge/audio_components.hpp>
#include <forge/project_paths.hpp>
namespace forge::asset_detail {
namespace {
std::variant<AudioClipData, CachedArtifact>
load_audio_revision(const std::filesystem::path& project, const AssetRecord& record) {
    if (record.type != AudioClipAsset::type || record.schema_version != 1 || !record.id)
        throw std::runtime_error("Missing or incompatible AudioClip asset");
    if (const auto selected = record.metadata.find("forge.import");
        selected != record.metadata.end()) {
        const auto* profile =
            selected->is_object()
                ? audio_source_profile(selected->at("importer").get<std::string>())
                : nullptr;
        if (!profile)
            throw std::runtime_error("AudioClip has an incompatible published importer");
        auto artifact =
            DerivedDataCache(project, {max_audio_pcm_bytes + 65536, max_audio_pcm_bytes + 65536, 2})
                .load_selected(selected->at("key").get<std::string>(), [](const auto& candidate) {
                    (void)validate_audio_bundle(candidate.files);
                });
        if (selected->at("output_version") != 1 ||
            selected->at("artifact_digest") != asset_build_digest(artifact.manifest.at("files")))
            throw std::runtime_error("AudioClip catalog/artifact revision mismatch");
        const auto metadata = validate_audio_bundle(artifact.files);
        if (metadata != record.metadata.at("forge.audio") ||
            metadata.at("source_digest") != selected->at("source_digest") ||
            metadata.at("source_format") != profile->extension.substr(1))
            throw std::runtime_error(
                "Selected AudioClip metadata does not match its cooked revision");
        return artifact;
    }
    // Compatibility for existing Phase6C records: no silent migration or source
    // writes. New imports select cooked data; legacy WAV is still bounded.
    auto extension = record.source.extension().string();
    for (auto& c : extension)
        if (c >= 'A' && c <= 'Z')
            c = char(c - 'A' + 'a');
    if (extension != ".wav")
        throw std::runtime_error("Legacy AudioClip requires a WAV source");
    return decode_audio_wav(
        read_bytes(ProjectPaths(project).resolve(record.source), max_audio_source_bytes));
}
} // namespace
AudioClipData load_audio_clip(const std::filesystem::path& project, const AssetRecord& record) {
    auto revision = load_audio_revision(project, record);
    if (auto* clip = std::get_if<AudioClipData>(&revision))
        return std::move(*clip);
    return audio_bundle_clip(std::get<CachedArtifact>(revision).files);
}
std::unique_ptr<AudioClipResource> load_audio_resource(const std::filesystem::path& project,
                                                       const AssetRecord& record,
                                                       std::shared_ptr<AudioStreamBudget> budget,
                                                       std::stop_token stop) {
    if (stop.stop_requested())
        throw std::runtime_error("Audio resource preparation cancelled");
    auto revision = load_audio_revision(project, record);
    if (stop.stop_requested())
        throw std::runtime_error("Audio resource preparation cancelled");
    if (auto* clip = std::get_if<AudioClipData>(&revision))
        return std::make_unique<AudioClipResource>(std::move(*clip));
    auto& files = std::get<CachedArtifact>(revision).files;
    const auto storage = audio_playback_storage(
        record.metadata.at("forge.audio").value("playback_storage", std::string("resident")));
    if (storage == AudioPlaybackStorage::Stream)
        return std::make_unique<AudioClipResource>(
            AudioStreamFile::create(audio_bundle_clip_bytes(files), std::move(budget), stop));
    return std::make_unique<AudioClipResource>(audio_bundle_clip(files));
}
} // namespace forge::asset_detail
