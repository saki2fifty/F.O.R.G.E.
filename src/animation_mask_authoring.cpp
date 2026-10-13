#include "animation_mask_authoring.hpp"
#include "animation_mask_binding.hpp"
#include <algorithm>
namespace forge {
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
constexpr const char* importer_id = "forge.animation.mask";
constexpr const char* recipe = "forge.animation.mask.v1";
struct Snapshot {
    AnimationMaskSource source;
    AssetBuildInput input;
};
Snapshot capture(const AssetImportRequest& request, std::stop_token stop) {
    require(!stop.stop_requested(), "Animation Mask import cancelled");
    const auto bytes = asset_detail::read_bytes(
        ProjectPaths(request.project).resolve(request.source), animation_mask_byte_limit);
    Snapshot result{AnimationMaskSource::parse(bytes), {}};
    require(result.source.asset() == request.asset,
            "Animation Mask source/catalog identity mismatch");
    const auto mask = result.source.compiled();
    const auto catalog = AssetCatalog::open_project(request.project);
    const auto skeleton = catalog.resolve(mask.skeleton, SkeletonAsset::type);
    require(skeleton.state == AssetState::Available && skeleton.record->subasset.has_value(),
            "Animation Mask needs a skeleton from an imported model");
    const auto selected = asset_detail::load_model_selection(
        request.project, catalog, skeleton.record->subasset->owner, stop);
    const auto joints = asset_detail::animation_mask_joints(selected, mask.skeleton);
    (void)mask.weights(mask.skeleton, joints.assets, joints.parents);
    auto& input = result.input;
    input.source_digest = asset_detail::content_digest(bytes);
    input.importer = importer_id;
    input.importer_revision = recipe;
    input.output_format = "forge.animation-mask";
    input.output_version = 1;
    input.platform = request.target.platform;
    input.backend = request.target.backend;
    input.profile = request.target.profile;
    auto dependency = [&](AssetId id, const char* type, std::string role) {
        const auto value = catalog.resolve(id, type);
        require(value.state == AssetState::Available && value.record->subasset &&
                    value.record->subasset->owner == selected.owner,
                "Animation Mask dependency is unavailable or belongs to another model");
        const auto imported = value.record->metadata.at("forge.import");
        require(imported.at("key") == selected.revision &&
                    imported.at("generation") == selected.generation,
                "Animation Mask dependencies mix model publication revisions");
        input.dependencies.push_back(
            {id, type, AssetDependencyKind::Runtime, std::move(role), selected.revision});
    };
    dependency(mask.skeleton, SkeletonAsset::type, "mask.skeleton");
    for (const auto& entry : mask.entries)
        dependency(entry.joint, ModelNodeAsset::type, "mask.joint:" + entry.joint.str());
    require(!stop.stop_requested(), "Animation Mask import cancelled");
    return result;
}
const ArtifactFile& mask_file(const std::vector<ArtifactFile>& files) {
    require(files.size() == 1 && files[0].name == "mask.json",
            "Animation Mask artifact must contain one cooked mask");
    const auto source = AnimationMaskSource::parse_cooked(files[0].bytes);
    (void)source.compiled();
    return files[0];
}
class AnimationMaskImporter final : public AssetImporter {
  public:
    AnimationMaskImporter()
        : AssetImporter(
              [] {
                  AssetImporterDescriptor descriptor;
                  descriptor.id = importer_id;
                  descriptor.revision = recipe;
                  descriptor.label = "Animation Mask";
                  descriptor.description = "Cook reusable weighted influences for imported bones.";
                  descriptor.extensions = {".json"};
                  descriptor.source_kinds = {"forge.animation-mask"};
                  descriptor.output_types = {AnimationMaskAsset::type};
                  descriptor.output_format = "forge.animation-mask";
                  descriptor.output_version = 1;
                  descriptor.targets = {{"windows", "d3d12", "desktop"}, {"linux", "none", "cpu"}};
                  descriptor.execution = ImportExecution::TrustedCpuTask;
                  descriptor.limits.output_files = 1;
                  descriptor.limits.output_bytes = animation_mask_cooked_byte_limit;
                  return descriptor;
              }(),
              {importer_id, 1, {}}) {}
    ImportProbeResult probe(const ImportProbe& source) const override {
        return path_utf8(source.source).ends_with(".mask.json")
                   ? ImportProbeResult{ImportProbeMatch::Possible, "forge.animation-mask",
                                       "Animation Mask source suffix"}
                   : ImportProbeResult{};
    }
    AssetImportPlan discover(const AssetImportRequest& request,
                             std::stop_token stop) const override {
        require(std::find(descriptor().targets.begin(), descriptor().targets.end(),
                          request.target) != descriptor().targets.end(),
                "Unsupported Animation Mask target");
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
                "Animation Mask changed after import discovery");
        if (progress)
            progress(.5, "Validating weighted bone influences");
        auto cooked = snapshot.source.cook();
        require(!stop.stop_requested(), "Animation Mask import cancelled");
        return {{"mask.json", std::move(cooked)}};
    }
    void validate(const CachedArtifact& artifact) const override {
        (void)mask_file(artifact.files);
    }
};
} // namespace
std::shared_ptr<const AssetImporterRegistry> animation_mask_import_registry() {
    auto registry = std::make_shared<AssetImporterRegistry>();
    registry->add(std::make_shared<AnimationMaskImporter>());
    registry->seal();
    return registry;
}
void prepare_animation_mask_publication(AssetPublicationCandidate& candidate,
                                        const AssetImportPlan& plan) {
    require(plan.input.importer == importer_id &&
                plan.data.at("asset").get<AssetId>() == candidate.ticket.owner &&
                candidate.sidecar.identity.entries.empty(),
            "Animation Mask publication identity/family mismatch");
    const auto& file = mask_file(candidate.files);
    (void)decode_animation_mask(file.bytes, candidate.ticket.owner);
    auto& identity = candidate.sidecar.identity;
    identity.owner = candidate.ticket.owner;
    identity.source = candidate.ticket.source;
    identity.source_digest = candidate.input.source_digest;
    identity.evidence_schema = "forge.animation-mask.single.v1";
    candidate.records = {{candidate.ticket.owner,
                          AnimationMaskAsset::type,
                          candidate.ticket.source,
                          1,
                          {},
                          {{"forge.animation_mask",
                            {{"version", 1},
                             {"file", file.name},
                             {"sha256", asset_detail::content_digest(file.bytes)},
                             {"bytes", file.bytes.size()}}}}}};
}
} // namespace forge
