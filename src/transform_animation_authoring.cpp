#include "transform_animation_authoring.hpp"
#include "asset_bytes.hpp"
#include "bounded_json.hpp"
#include <algorithm>

namespace forge {
namespace {
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
constexpr const char* importer_id = "forge.animation.transform";
constexpr const char* recipe = "forge.animation.transform.v2";
struct Snapshot {
    TransformAnimationSource source;
    AssetBuildInput input;
};
Snapshot capture(const AssetImportRequest& request, std::stop_token stop) {
    require(!stop.stop_requested(), "Transform animation import cancelled");
    const auto bytes = asset_detail::read_bytes(
        ProjectPaths(request.project).resolve(request.source), transform_animation_byte_limit);
    Snapshot result{TransformAnimationSource::parse(bytes), {}};
    require(result.source.asset() == request.asset,
            "Transform animation source/catalog identity mismatch");
    auto& input = result.input;
    input.source_digest = asset_detail::content_digest(bytes);
    input.importer = importer_id;
    input.importer_revision = recipe;
    input.output_format = "forge.transform-clip";
    input.output_version = 2;
    input.platform = request.target.platform;
    input.backend = request.target.backend;
    input.profile = request.target.profile;
    return result;
}
const ArtifactFile& clip_file(const std::vector<ArtifactFile>& files) {
    require(files.size() == 1 && files[0].name == "animation.clip",
            "Transform animation artifact must contain one cooked animation clip");
    auto doc = asset_detail::parse_bounded_json(files[0].bytes, transform_animation_byte_limit);
    (void)decode_transform_animation(files[0].bytes, doc.at("asset_id").get<AssetId>());
    return files[0];
}
class TransformAnimationImporter final : public AssetImporter {
  public:
    TransformAnimationImporter()
        : AssetImporter(
              [] {
                  AssetImporterDescriptor d;
                  d.id = importer_id;
                  d.revision = recipe;
                  d.label = "Transform animation";
                  d.description =
                      "Cook authored transform keys into a reusable runtime animation clip.";
                  d.extensions = {".json"};
                  d.source_kinds = {"forge.transform-animation"};
                  d.output_types = {TransformAnimationAsset::type};
                  d.output_format = "forge.transform-clip";
                  d.output_version = 2;
                  d.targets = {{"windows", "d3d12", "desktop"}, {"linux", "none", "cpu"}};
                  d.execution = ImportExecution::TrustedCpuTask;
                  d.limits.output_files = 1;
                  d.limits.output_bytes = 32 * 1024 * 1024;
                  return d;
              }(),
              {importer_id, 1, {}}) {}
    ImportProbeResult probe(const ImportProbe& source) const override {
        return path_utf8(source.source).ends_with(".animation.json")
                   ? ImportProbeResult{ImportProbeMatch::Possible, "forge.transform-animation",
                                       "Transform animation source document suffix"}
                   : ImportProbeResult{};
    }
    AssetImportPlan discover(const AssetImportRequest& request,
                             std::stop_token stop) const override {
        require(std::find(descriptor().targets.begin(), descriptor().targets.end(),
                          request.target) != descriptor().targets.end(),
                "Unsupported Transform animation target");
        (void)settings().effective(request.settings);
        auto snapshot = capture(request, stop);
        return {std::move(snapshot.input), {}, {{"asset", request.asset}}};
    }
    std::vector<ArtifactFile>
    import_and_cook(const AssetImportRequest& request, const AssetImportPlan& plan,
                    std::stop_token stop,
                    const std::function<void(double, std::string)>& progress) const override {
        auto snapshot = capture(request, stop);
        require(snapshot.input.document() == plan.input.document(),
                "Transform animation source changed after import discovery");
        if (progress)
            progress(.5, "Validating transform keys and interpolation");
        const auto cooked = snapshot.source.cook();
        require(!stop.stop_requested(), "Transform animation import cancelled");
        return {{"animation.clip", cooked}};
    }
    void validate(const CachedArtifact& artifact) const override {
        (void)clip_file(artifact.files);
    }
};
} // namespace
std::shared_ptr<const AssetImporterRegistry> transform_animation_import_registry() {
    auto registry = std::make_shared<AssetImporterRegistry>();
    registry->add(std::make_shared<TransformAnimationImporter>());
    registry->seal();
    return registry;
}
void prepare_transform_animation_publication(AssetPublicationCandidate& candidate,
                                             const AssetImportPlan& plan) {
    require(plan.input.importer == importer_id &&
                plan.data.at("asset").get<AssetId>() == candidate.ticket.owner &&
                candidate.sidecar.identity.entries.empty(),
            "Transform animation publication identity/family mismatch");
    const auto& file = clip_file(candidate.files);
    (void)decode_transform_animation(file.bytes, candidate.ticket.owner);
    auto& identity = candidate.sidecar.identity;
    identity.owner = candidate.ticket.owner;
    identity.source = candidate.ticket.source;
    identity.source_digest = candidate.input.source_digest;
    identity.evidence_schema = "forge.transform-animation.single.v1";
    candidate.records = {{candidate.ticket.owner,
                          TransformAnimationAsset::type,
                          candidate.ticket.source,
                          1,
                          {},
                          {{"forge.transform_animation",
                            {{"version", 1},
                             {"file", file.name},
                             {"sha256", asset_detail::content_digest(file.bytes)},
                             {"bytes", file.bytes.size()}}}}}};
}
} // namespace forge
