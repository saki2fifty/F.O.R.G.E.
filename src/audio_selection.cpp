#include "audio_selection.hpp"
#include "asset_bytes.hpp"
#include "audio_bundle.hpp"
#include <forge/audio_components.hpp>
#include <forge/project_paths.hpp>
namespace forge::asset_detail {
AudioClipData load_audio_clip(const std::filesystem::path& project, const AssetRecord& record) {
    if (record.type != AudioClipAsset::type || record.schema_version != 1 || !record.id)
        throw std::runtime_error("Missing or incompatible AudioClip asset");
    if (const auto selected = record.metadata.find("forge.import");
        selected != record.metadata.end()) {
        if (!selected->is_object() || selected->at("importer") != "forge.audio.wav")
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
            metadata.at("source_digest") != selected->at("source_digest"))
            throw std::runtime_error(
                "Selected AudioClip metadata does not match its cooked revision");
        return audio_bundle_clip(artifact.files);
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
} // namespace forge::asset_detail
