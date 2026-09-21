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
AssetImporterDescriptor descriptor() {
    AssetImporterDescriptor d;
    d.id = "forge.audio.wav";
    d.revision = audio_recipe_revision();
    d.label = "WAV AudioClip";
    d.description =
        "Validate WAV with miniaudio and publish immutable mono/stereo PCM and metadata.";
    d.extensions = {".wav"};
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

  public:
    explicit AudioImporter(std::filesystem::path worker)
        : AssetImporter(asset_detail::descriptor(), audio_settings()), worker_(std::move(worker)) {}
    ImportProbeResult probe(const ImportProbe& input) const override {
        // WAV also permits RF64/Wave64. Let the bounded official decoder perform
        // full admission rather than inventing another RIFF parser here.
        auto extension = input.source.extension().string();
        for (auto& c : extension)
            if (c >= 'A' && c <= 'Z')
                c = char(c - 'A' + 'a');
        return extension == ".wav" ? ImportProbeResult{ImportProbeMatch::Possible, "audio",
                                                       "WAV requires miniaudio admission"}
                                   : ImportProbeResult{};
    }
    AssetImportPlan discover(const AssetImportRequest& request,
                             std::stop_token stop) const override {
        require(!stop.stop_requested(), "Audio discovery cancelled");
        const auto locator = ProjectPaths::normalize(request.source);
        const auto bytes =
            read_bytes(ProjectPaths(request.project).resolve(locator), max_audio_source_bytes);
        require(probe({locator, {}}).match != ImportProbeMatch::No, "Select a WAV source");
        require(bytes.size() >= 12, "WAV source is truncated");
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
                "WAV changed during snapshot capture");
        if (progress)
            progress(.15, "Validating audio in isolated worker");
        Json payload{{"recipe", "forge.audio.wav"},
                     {"revision", audio_recipe_revision()},
                     {"source_digest", plan.input.source_digest},
                     {"settings", request.settings}};
        auto files = run_import_process(worker_, request.project,
                                        {std::move(payload), {{"source.wav", std::move(bytes)}}},
                                        audio_worker_limits(), stop);
        const auto metadata = validate_audio_bundle(files);
        require(metadata.at("source_digest") == plan.input.source_digest,
                "Audio worker returned another source revision");
        if (progress)
            progress(1., "AudioClip validated");
        return files;
    }
    void validate(const CachedArtifact& candidate) const override {
        (void)validate_audio_bundle(candidate.files);
    }
};
} // namespace
ImportSettingsSchema audio_settings() { return {"forge.audio.wav", 1, {}}; }
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
std::shared_ptr<const AssetImporter> audio_importer(std::filesystem::path worker) {
    return std::make_shared<AudioImporter>(std::move(worker));
}
} // namespace forge::asset_detail
