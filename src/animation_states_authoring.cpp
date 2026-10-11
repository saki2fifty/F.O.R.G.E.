#include "animation_states_authoring.hpp"
#include "asset_bytes.hpp"
#include "bounded_json.hpp"
#include <algorithm>

namespace forge {
namespace {
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
constexpr const char* importer_id = "forge.animation.states";
constexpr const char* recipe = "forge.animation.states.v2";
struct Snapshot {
    AnimationStatesSource source;
    AssetBuildInput input;
};
Snapshot capture(const AssetImportRequest& request, std::stop_token stop) {
    require(!stop.stop_requested(), "Animation States import cancelled");
    const auto bytes = asset_detail::read_bytes(
        ProjectPaths(request.project).resolve(request.source), animation_states_byte_limit);
    Snapshot result{AnimationStatesSource::parse(bytes), {}};
    require(result.source.asset() == request.asset,
            "Animation States source/catalog identity mismatch");
    auto& input = result.input;
    input.source_digest = asset_detail::content_digest(bytes);
    input.importer = importer_id;
    input.importer_revision = recipe;
    input.output_format = "forge.animation-states";
    input.output_version = 1;
    input.platform = request.target.platform;
    input.backend = request.target.backend;
    input.profile = request.target.profile;
    const auto compiled = result.source.compiled();
    const auto catalog = AssetCatalog::open_project(request.project);
    auto dependency = [&](AssetId id, const char* type, std::string role) {
        const auto resolved = catalog.resolve(id, type);
        require(resolved.state == AssetState::Available,
                "Animation States dependency is unavailable");
        const auto& record = *resolved.record;
        const auto revision = record.metadata.at("forge.import").at("key").get<std::string>();
        input.dependencies.push_back(
            {id, type, AssetDependencyKind::Runtime, std::move(role), revision});
    };
    const auto skeleton = catalog.resolve(compiled.skeleton, SkeletonAsset::type);
    require(skeleton.state == AssetState::Available && skeleton.record->subasset.has_value(),
            "Animation States needs a skeleton from an imported model");
    const auto& family = *skeleton.record;
    dependency(compiled.skeleton, SkeletonAsset::type, "states.skeleton");
    for (const auto& state : compiled.states) {
        const auto clip = catalog.resolve(state.clip);
        require(clip.state == AssetState::Available && clip.record->subasset &&
                    clip.record->subasset->owner == family.subasset->owner &&
                    clip.record->metadata.at("forge.import").at("key") ==
                        family.metadata.at("forge.import").at("key") &&
                    clip.record->metadata.at("forge.import").at("generation") ==
                        family.metadata.at("forge.import").at("generation"),
                "All animation states must come from the same exact imported model revision");
        dependency(state.clip.id, AnimationClipAsset::type,
                   "states.clip:" + std::to_string(state.id));
    }
    return result;
}
const ArtifactFile& clip_file(const std::vector<ArtifactFile>& files) {
    require(files.size() == 1 && files[0].name == "states.json",
            "Animation States artifact must contain one cooked state set");
    auto doc = asset_detail::parse_bounded_json(files[0].bytes, animation_states_byte_limit);
    (void)decode_animation_states(files[0].bytes, doc.at("asset_id").get<AssetId>());
    return files[0];
}
class AnimationStatesImporter final : public AssetImporter {
  public:
    AnimationStatesImporter()
        : AssetImporter(
              [] {
                  AssetImporterDescriptor d;
                  d.id = importer_id;
                  d.revision = recipe;
                  d.label = "Animation States";
                  d.description = "Cook a named skeletal clip set for C++ state selection.";
                  d.extensions = {".json"};
                  d.source_kinds = {"forge.animation-states"};
                  d.output_types = {AnimationStatesAsset::type};
                  d.output_format = "forge.animation-states";
                  d.output_version = 1;
                  d.targets = {{"windows", "d3d12", "desktop"}, {"linux", "none", "cpu"}};
                  d.execution = ImportExecution::TrustedCpuTask;
                  d.limits.output_files = 1;
                  d.limits.output_bytes = animation_states_byte_limit;
                  return d;
              }(),
              {importer_id, 1, {}}) {}
    ImportProbeResult probe(const ImportProbe& source) const override {
        return path_utf8(source.source).ends_with(".states.json")
                   ? ImportProbeResult{ImportProbeMatch::Possible, "forge.animation-states",
                                       "Animation States source document suffix"}
                   : ImportProbeResult{};
    }
    AssetImportPlan discover(const AssetImportRequest& request,
                             std::stop_token stop) const override {
        require(std::find(descriptor().targets.begin(), descriptor().targets.end(),
                          request.target) != descriptor().targets.end(),
                "Unsupported Animation States target");
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
                "Animation States source changed after import discovery");
        if (progress)
            progress(.5, "Validating named animation states");
        const auto cooked = snapshot.source.cook();
        require(!stop.stop_requested(), "Animation States import cancelled");
        return {{"states.json", cooked}};
    }
    void validate(const CachedArtifact& artifact) const override {
        (void)clip_file(artifact.files);
    }
};
} // namespace
std::shared_ptr<const AssetImporterRegistry> animation_states_import_registry() {
    auto registry = std::make_shared<AssetImporterRegistry>();
    registry->add(std::make_shared<AnimationStatesImporter>());
    registry->seal();
    return registry;
}
void prepare_animation_states_publication(AssetPublicationCandidate& candidate,
                                          const AssetImportPlan& plan) {
    require(plan.input.importer == importer_id &&
                plan.data.at("asset").get<AssetId>() == candidate.ticket.owner &&
                candidate.sidecar.identity.entries.empty(),
            "Animation States publication identity/family mismatch");
    const auto& file = clip_file(candidate.files);
    (void)decode_animation_states(file.bytes, candidate.ticket.owner);
    auto& identity = candidate.sidecar.identity;
    identity.owner = candidate.ticket.owner;
    identity.source = candidate.ticket.source;
    identity.source_digest = candidate.input.source_digest;
    identity.evidence_schema = "forge.animation-states.single.v1";
    candidate.records = {{candidate.ticket.owner,
                          AnimationStatesAsset::type,
                          candidate.ticket.source,
                          1,
                          {},
                          {{"forge.animation_states",
                            {{"version", 1},
                             {"file", file.name},
                             {"sha256", asset_detail::content_digest(file.bytes)},
                             {"bytes", file.bytes.size()}}}}}};
}
} // namespace forge
