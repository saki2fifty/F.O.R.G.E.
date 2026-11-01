#pragma once
#include "asset_bytes.hpp"
#include "asset_import_service.hpp"
#include <algorithm>

namespace forge {
// One logical JSON source and one cooked file. Codecs retain format validation;
// AssetImportService retains discovery, cache, ticket and publication ownership.
template <class Source> struct SingleSourceImportProfile {
    std::string importer, revision, label, description, suffix, format, asset_type, file,
        evidence_schema, metadata_key;
    std::size_t source_limit = 0, cooked_limit = 0;
    std::function<Source(std::span<const std::byte>)> parse_cooked;
};
namespace asset_detail {
template <class Source>
const ArtifactFile& single_source_file(const std::vector<ArtifactFile>& files,
                                       const SingleSourceImportProfile<Source>& profile) {
    if (files.size() != 1 || files.front().name != profile.file)
        throw std::runtime_error(profile.label + " requires one " + profile.file);
    (void)profile.parse_cooked(files.front().bytes);
    return files.front();
}
template <class Source> class SingleSourceImporter final : public AssetImporter {
    SingleSourceImportProfile<Source> profile_;
    struct Capture {
        Source source;
        AssetBuildInput input;
    };
    static AssetImporterDescriptor descriptor_for(const SingleSourceImportProfile<Source>& p) {
        AssetImporterDescriptor d;
        d.id = p.importer;
        d.revision = p.revision;
        d.label = p.label;
        d.description = p.description;
        d.extensions = {".json"};
        d.source_kinds = {p.format};
        d.output_types = {p.asset_type};
        d.output_format = p.format;
        d.output_version = 1;
        d.execution = ImportExecution::TrustedCpuTask;
        d.targets = {{"windows", "d3d12", "desktop"}, {"linux", "none", "cpu"}};
        d.limits.output_files = 1;
        d.limits.output_bytes = p.cooked_limit;
        return d;
    }
    Capture capture(const AssetImportRequest& request, std::stop_token stop) const {
        if (stop.stop_requested())
            throw std::runtime_error(profile_.label + " import cancelled");
        const auto bytes = read_bytes(ProjectPaths(request.project).resolve(request.source),
                                      profile_.source_limit);
        Capture result{Source::parse(bytes), {}};
        if (result.source.asset() != request.asset)
            throw std::runtime_error(profile_.label + " source/catalog identity mismatch");
        result.input.source_digest = content_digest(bytes);
        result.input.importer = profile_.importer;
        result.input.importer_revision = profile_.revision;
        result.input.output_format = profile_.format;
        result.input.output_version = 1;
        result.input.platform = request.target.platform;
        result.input.backend = request.target.backend;
        result.input.profile = request.target.profile;
        return result;
    }

  public:
    explicit SingleSourceImporter(SingleSourceImportProfile<Source> profile)
        : AssetImporter(descriptor_for(profile), {profile.importer, 1, {}}),
          profile_(std::move(profile)) {}
    ImportProbeResult probe(const ImportProbe& value) const override {
        return path_utf8(value.source).ends_with(profile_.suffix)
                   ? ImportProbeResult{ImportProbeMatch::Possible, profile_.format,
                                       profile_.label + " source suffix"}
                   : ImportProbeResult{};
    }
    AssetImportPlan discover(const AssetImportRequest& request,
                             std::stop_token stop) const override {
        if (std::find(descriptor().targets.begin(), descriptor().targets.end(), request.target) ==
            descriptor().targets.end())
            throw std::runtime_error("Unsupported " + profile_.label + " target");
        (void)settings().effective(request.settings);
        auto value = capture(request, stop);
        return {std::move(value.input), {}, {{"asset", request.asset}}};
    }
    std::vector<ArtifactFile>
    import_and_cook(const AssetImportRequest& request, const AssetImportPlan& plan,
                    std::stop_token stop,
                    const std::function<void(double, std::string)>& progress) const override {
        auto value = capture(request, stop);
        if (value.input.document() != plan.input.document())
            throw std::runtime_error(profile_.label + " source changed after discovery");
        if (progress)
            progress(.5, "Validating " + profile_.label);
        auto bytes = value.source.cook();
        if (stop.stop_requested())
            throw std::runtime_error(profile_.label + " import cancelled");
        return {{profile_.file, std::move(bytes)}};
    }
    void validate(const CachedArtifact& artifact) const override {
        (void)single_source_file(artifact.files, profile_);
    }
};
} // namespace asset_detail
template <class Source>
std::shared_ptr<const AssetImporterRegistry>
single_source_import_registry(const SingleSourceImportProfile<Source>& profile) {
    auto registry = std::make_shared<AssetImporterRegistry>();
    registry->add(std::make_shared<asset_detail::SingleSourceImporter<Source>>(profile));
    registry->seal();
    return registry;
}
template <class Source>
void prepare_single_source_publication(AssetPublicationCandidate& candidate,
                                       const AssetImportPlan& plan,
                                       const SingleSourceImportProfile<Source>& profile) {
    if (plan.input.importer != profile.importer ||
        plan.data.at("asset").template get<AssetId>() != candidate.ticket.owner ||
        !candidate.sidecar.identity.entries.empty())
        throw std::runtime_error(profile.label + " publication identity/family mismatch");
    const auto& file = asset_detail::single_source_file(candidate.files, profile);
    if (profile.parse_cooked(file.bytes).asset() != candidate.ticket.owner)
        throw std::runtime_error(profile.label + " cooked/publication identity mismatch");
    auto& identity = candidate.sidecar.identity;
    identity.owner = candidate.ticket.owner;
    identity.source = candidate.ticket.source;
    identity.source_digest = candidate.input.source_digest;
    identity.evidence_schema = profile.evidence_schema;
    candidate.records = {{candidate.ticket.owner,
                          profile.asset_type,
                          candidate.ticket.source,
                          1,
                          {},
                          {{profile.metadata_key,
                            {{"version", 1},
                             {"file", file.name},
                             {"sha256", asset_detail::content_digest(file.bytes)},
                             {"bytes", file.bytes.size()}}}}}};
}
} // namespace forge
