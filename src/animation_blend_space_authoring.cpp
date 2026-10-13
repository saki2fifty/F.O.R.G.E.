#include "animation_blend_space_authoring.hpp"
#include "model_selection.hpp"
#include <algorithm>
namespace forge {
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
constexpr const char* importer_id = "forge.animation.blend-space";
constexpr const char* recipe = "forge.animation.blend-space.v1";
struct Snapshot {
    AnimationBlendSpaceSource source;
    AssetBuildInput input;
};
Snapshot capture(const AssetImportRequest& request, std::stop_token stop) {
    require(!stop.stop_requested(), "Animation Blend Space import cancelled");
    const auto bytes = asset_detail::read_bytes(
        ProjectPaths(request.project).resolve(request.source), animation_blend_space_byte_limit);
    Snapshot result{AnimationBlendSpaceSource::parse(bytes), {}};
    require(result.source.asset() == request.asset,
            "Animation Blend Space source/catalog identity mismatch");
    const auto space = result.source.compiled();
    const auto catalog = AssetCatalog::open_project(request.project);
    const auto skeleton = catalog.resolve(space.skeleton, SkeletonAsset::type);
    require(skeleton.state == AssetState::Available && skeleton.record->subasset.has_value(),
            "Animation Blend Space needs a skeleton from an imported model");
    const auto selected = asset_detail::load_model_selection(
        request.project, catalog, skeleton.record->subasset->owner, stop);
    require(selected.member(space.skeleton).identity.type == SkeletonAsset::type,
            "Blend Space skeleton is not an admitted model skeleton");
    auto& input = result.input;
    input.source_digest = asset_detail::content_digest(bytes);
    input.importer = importer_id;
    input.importer_revision = recipe;
    input.output_format = "forge.animation-blend-space";
    input.output_version = 1;
    input.platform = request.target.platform;
    input.backend = request.target.backend;
    input.profile = request.target.profile;
    auto dependency = [&](AssetId id, const char* type, std::string role) {
        const auto value = catalog.resolve(id, type);
        require(value.state == AssetState::Available && value.record->subasset &&
                    value.record->subasset->owner == selected.owner,
                "Animation Blend Space dependency is unavailable or belongs to another model");
        const auto imported = value.record->metadata.at("forge.import");
        require(imported.at("key") == selected.revision &&
                    imported.at("generation") == selected.generation,
                "Animation Blend Space dependencies mix model publication revisions");
        input.dependencies.push_back(
            {id, type, AssetDependencyKind::Runtime, std::move(role), selected.revision});
    };
    dependency(space.skeleton, SkeletonAsset::type, "space.skeleton");
    for (const auto& sample : space.samples) {
        require(selected.member(sample.clip.id).identity.type == AnimationClipAsset::type,
                "Blend Space sample is not an admitted model clip");
        dependency(sample.clip.id, AnimationClipAsset::type, "space.clip:" + sample.id.str());
    }
    require(!stop.stop_requested(), "Animation Blend Space import cancelled");
    return result;
}
const ArtifactFile& space_file(const std::vector<ArtifactFile>& files) {
    require(files.size() == 1 && files[0].name == "blendspace.json",
            "Animation Blend Space artifact must contain one cooked blend space");
    const auto source = AnimationBlendSpaceSource::parse_cooked(files[0].bytes);
    (void)source.compiled();
    return files[0];
}
class AnimationBlendSpaceImporter final : public AssetImporter {
  public:
    AnimationBlendSpaceImporter()
        : AssetImporter(
              [] {
                  AssetImporterDescriptor descriptor;
                  descriptor.id = importer_id;
                  descriptor.revision = recipe;
                  descriptor.label = "Animation Blend Space";
                  descriptor.description =
                      "Cook compatible clips along one numeric animation axis.";
                  descriptor.extensions = {".json"};
                  descriptor.source_kinds = {"forge.animation-blend-space"};
                  descriptor.output_types = {AnimationBlendSpaceAsset::type};
                  descriptor.output_format = "forge.animation-blend-space";
                  descriptor.output_version = 1;
                  descriptor.targets = {{"windows", "d3d12", "desktop"}, {"linux", "none", "cpu"}};
                  descriptor.execution = ImportExecution::TrustedCpuTask;
                  descriptor.limits.output_files = 1;
                  descriptor.limits.output_bytes = animation_blend_space_cooked_byte_limit;
                  return descriptor;
              }(),
              {importer_id, 1, {}}) {}
    ImportProbeResult probe(const ImportProbe& source) const override {
        return path_utf8(source.source).ends_with(".blendspace.json")
                   ? ImportProbeResult{ImportProbeMatch::Possible, "forge.animation-blend-space",
                                       "Animation Blend Space source suffix"}
                   : ImportProbeResult{};
    }
    AssetImportPlan discover(const AssetImportRequest& request,
                             std::stop_token stop) const override {
        require(std::find(descriptor().targets.begin(), descriptor().targets.end(),
                          request.target) != descriptor().targets.end(),
                "Unsupported Animation Blend Space target");
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
                "Animation Blend Space changed after import discovery");
        if (progress)
            progress(.5, "Validating neighboring animation samples");
        auto cooked = snapshot.source.cook();
        require(!stop.stop_requested(), "Animation Blend Space import cancelled");
        return {{"blendspace.json", std::move(cooked)}};
    }
    void validate(const CachedArtifact& artifact) const override {
        (void)space_file(artifact.files);
    }
};
} // namespace
std::shared_ptr<const AssetImporterRegistry> animation_blend_space_import_registry() {
    auto registry = std::make_shared<AssetImporterRegistry>();
    registry->add(std::make_shared<AnimationBlendSpaceImporter>());
    registry->seal();
    return registry;
}
void prepare_animation_blend_space_publication(AssetPublicationCandidate& candidate,
                                               const AssetImportPlan& plan) {
    require(plan.input.importer == importer_id &&
                plan.data.at("asset").get<AssetId>() == candidate.ticket.owner &&
                candidate.sidecar.identity.entries.empty(),
            "Animation Blend Space publication identity/family mismatch");
    const auto& file = space_file(candidate.files);
    (void)decode_animation_blend_space(file.bytes, candidate.ticket.owner);
    auto& identity = candidate.sidecar.identity;
    identity.owner = candidate.ticket.owner;
    identity.source = candidate.ticket.source;
    identity.source_digest = candidate.input.source_digest;
    identity.evidence_schema = "forge.animation-blend-space.single.v1";
    candidate.records = {{candidate.ticket.owner,
                          AnimationBlendSpaceAsset::type,
                          candidate.ticket.source,
                          1,
                          {},
                          {{"forge.animation_blend_space",
                            {{"version", 1},
                             {"file", file.name},
                             {"sha256", asset_detail::content_digest(file.bytes)},
                             {"bytes", file.bytes.size()}}}}}};
}
} // namespace forge
