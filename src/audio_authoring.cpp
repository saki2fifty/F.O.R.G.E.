#include "audio_authoring.hpp"
#include "audio_bundle.hpp"
#include "audio_importer.hpp"
namespace forge {
std::shared_ptr<const AssetImporterRegistry>
audio_import_registry(const std::filesystem::path& worker) {
    auto registry = std::make_shared<AssetImporterRegistry>();
    for (const auto& profile : asset_detail::audio_source_profiles)
        registry->add(asset_detail::audio_importer(worker, profile));
    registry->seal();
    return registry;
}
void prepare_audio_publication(AssetPublicationCandidate& candidate, const AssetImportPlan& plan) {
    if (plan.input.output_format != "forge.audio-clip" ||
        !candidate.sidecar.identity.entries.empty())
        throw std::runtime_error("Audio publication cannot replace another asset family");
    auto metadata = asset_detail::validate_audio_bundle(candidate.files);
    const auto* profile = asset_detail::audio_source_profile(plan.input.importer);
    if (!profile || metadata.at("source_format") != nlohmann::json(profile->extension.substr(1)))
        throw std::runtime_error("AudioClip metadata belongs to another source format");
    if (metadata.at("source_digest") != candidate.input.source_digest)
        throw std::runtime_error("AudioClip metadata belongs to another source revision");
    auto& identity = candidate.sidecar.identity;
    identity.owner = candidate.ticket.owner;
    identity.source = candidate.ticket.source;
    identity.source_digest = candidate.input.source_digest;
    identity.evidence_schema = "forge.audio.single.v1";
    AssetRecord record{candidate.ticket.owner, "audio_clip", candidate.ticket.source, 1, {}};
    record.metadata["forge.audio"] = std::move(metadata);
    candidate.records = {std::move(record)};
}
} // namespace forge
