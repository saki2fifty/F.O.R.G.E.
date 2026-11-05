#include "audio_importer.hpp"
#include "asset_bytes.hpp"
#include "audio_bundle.hpp"
#include <algorithm>
namespace forge::asset_detail {
namespace {
using Json = nlohmann::json;
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
AssetImporterDescriptor descriptor(const AudioSourceProfile& profile) {
    AssetImporterDescriptor d;
    d.id = profile.importer;
    d.revision = audio_recipe_revision();
    d.label = std::string(profile.label) + " AudioClip";
    d.description = "Validate " + std::string(profile.label) +
                    " with miniaudio and publish immutable mono/stereo PCM and metadata.";
    d.extensions = {std::string(profile.extension)};
    d.source_kinds = {"audio"};
    d.output_types = {"audio_clip"};
    d.output_format = "forge.audio-clip";
    d.limits.memory_bytes = audio_worker_limits().memory_bytes;
    d.limits.seconds = audio_worker_limits().seconds;
    d.limits.output_bytes = max_audio_pcm_bytes + 65536;
    d.limits.output_files = 2;
    d.targets = {{"*", "*", "*"}};
    return d;
}
class AudioImporter final : public AssetImporter {
    std::filesystem::path worker_;
    AudioSourceProfile profile_;

  public:
    AudioImporter(std::filesystem::path worker, const AudioSourceProfile& profile)
        : AssetImporter(asset_detail::descriptor(profile), audio_settings(profile.importer)),
          worker_(std::move(worker)), profile_(profile) {}
    ImportProbeResult probe(const ImportProbe& input) const override {
        // The selected bounded decoder owns full admission, including WAV's
        // RF64/Wave64 variants. The extension never proves content validity.
        auto extension = input.source.extension().string();
        for (auto& c : extension)
            if (c >= 'A' && c <= 'Z')
                c = char(c - 'A' + 'a');
        return extension == profile_.extension
                   ? ImportProbeResult{ImportProbeMatch::Possible, "audio",
                                       "Audio requires miniaudio admission"}
                   : ImportProbeResult{};
    }
    AssetImportPlan discover(const AssetImportRequest& request,
                             std::stop_token stop) const override {
        require(!stop.stop_requested(), "Audio discovery cancelled");
        const auto locator = ProjectPaths::normalize(request.source);
        const auto bytes =
            read_bytes(ProjectPaths(request.project).resolve(locator), max_audio_source_bytes);
        require(probe({locator, {}}).match != ImportProbeMatch::No,
                "Select a source matching this audio importer");
        require(bytes.size() >= 12, "Audio source is truncated");
        AssetImportPlan plan;
        const auto& d = AssetImporter::descriptor();
        auto& input = plan.input;
        input.source_digest = content_digest(bytes);
        input.importer = d.id;
        input.importer_revision = d.revision;
        input.settings_version = settings().version();
        input.settings = settings().effective(request.settings);
        input.output_format = d.output_format;
        input.output_version = d.output_version;
        input.platform = request.target.platform;
        input.backend = request.target.backend;
        input.profile = request.target.profile;
        require(!stop.stop_requested(), "Audio discovery cancelled");
        return plan;
    }
    std::vector<ArtifactFile>
    import_and_cook(const AssetImportRequest& request, const AssetImportPlan& plan,
                    std::stop_token stop,
                    const std::function<void(double, std::string)>& progress) const override {
        require(discover(request, stop).input.document() == plan.input.document(),
                "Audio source/settings changed after discovery");
        auto bytes = read_bytes(ProjectPaths(request.project).resolve(request.source),
                                max_audio_source_bytes);
        require(content_digest(bytes) == plan.input.source_digest,
                "Audio changed during snapshot capture");
        if (progress)
            progress(.15, "Validating audio in isolated worker");
        Json payload{{"recipe", profile_.importer},
                     {"revision", audio_recipe_revision()},
                     {"source_digest", plan.input.source_digest},
                     {"settings", request.settings}};
        auto files = run_import_process(
            worker_, request.project,
            {std::move(payload), {{"source" + std::string(profile_.extension), std::move(bytes)}}},
            audio_worker_limits(), stop);
        const auto metadata = validate_audio_bundle(files);
        require(metadata.at("source_digest") == plan.input.source_digest,
                "Audio worker returned another source revision");
        require(metadata.at("source_format") == profile_.extension.substr(1),
                "Audio worker returned another source format");
        if (progress)
            progress(1., "AudioClip validated");
        return files;
    }
    void validate(const CachedArtifact& candidate) const override {
        const auto metadata = validate_audio_bundle(candidate.files);
        require(metadata.at("source_format") == profile_.extension.substr(1),
                "Audio artifact belongs to another source format");
    }
};
} // namespace
ImportSettingsSchema audio_settings(std::string_view importer) {
    require(audio_source_profile(importer) != nullptr, "Unsupported audio importer");
    std::vector<ImportSettingRule> rules{
        {"force_mono",
         "Force mono",
         "Average stereo channels into one channel during import. The source audio is unchanged.",
         ImportSettingType::Boolean,
         false,
         {},
         {},
         {}},
        {"normalize_peak",
         "Normalize peak",
         "Scale the imported peak to 1 before trimming. This is peak, not loudness, normalization.",
         ImportSettingType::Boolean,
         false,
         {},
         {},
         {}},
        {"trim_silence",
         "Trim silence",
         "Remove quiet leading/trailing frames and soften removed edges with a 5 ms fade. Entirely "
         "quiet clips become one silent frame.",
         ImportSettingType::Boolean,
         false,
         {},
         {},
         {}},
        {"playback_storage",
         "Playback storage",
         "Resident keeps decoded PCM in memory. Stream prepares 48000 Hz PCM for bounded disk "
         "playback; clips must be at least one second. The source is unchanged.",
         ImportSettingType::Choice,
         "resident",
         {},
         {},
         {"resident", "stream"}}};
    ImportSettingRule threshold{"trim_threshold_db",
                                "Trim threshold (dB)",
                                "Used only when Trim silence is enabled. Any channel above this "
                                "level keeps its frame. Normalization runs first.",
                                ImportSettingType::Number,
                                -60.,
                                {},
                                {},
                                {}};
    threshold.minimum = -96;
    threshold.maximum = -6;
    rules.push_back(std::move(threshold));
    return {std::string(importer), 1, std::move(rules)};
}
std::string audio_recipe_revision() {
    return asset_build_digest({{"source", FORGE_AUDIO_RECIPE_FINGERPRINT},
                               {"configuration", FORGE_AUDIO_RECIPE_CONFIGURATION}});
}
WorkerLimits audio_worker_limits() {
    WorkerLimits result;
    result.memory_bytes = 512ull * 1024 * 1024;
    result.file_bytes = max_audio_pcm_bytes + 65536;
    result.total_bytes = 96ull * 1024 * 1024;
    result.files = 8;
    result.cancellation_grace_ms = 500;
    return result;
}
std::shared_ptr<const AssetImporter> audio_importer(std::filesystem::path worker,
                                                    const AudioSourceProfile& profile) {
    const auto* supported = audio_source_profile(profile.importer);
    require(supported && supported->format == profile.format &&
                supported->extension == profile.extension && supported->label == profile.label,
            "Unsupported audio source profile");
    return std::make_shared<AudioImporter>(std::move(worker), *supported);
}
} // namespace forge::asset_detail
