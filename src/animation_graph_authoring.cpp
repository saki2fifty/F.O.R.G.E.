#include "animation_graph_authoring.hpp"
#include "animation_clip_selection.hpp"
#include "animation_mask_resource.hpp"
#include "model_skeleton_binding.hpp"
#include <algorithm>

namespace forge {
namespace {
constexpr auto importer_id = "forge.animation.graph";
constexpr auto recipe = "forge.animation.graph.v1";
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
struct Snapshot {
    AnimationGraphSource source;
    AssetBuildInput input;
};
Snapshot capture(const AssetImportRequest& request, std::stop_token stop) {
    require(!stop.stop_requested(), "Animation Graph import cancelled");
    const auto bytes = asset_detail::read_bytes(
        ProjectPaths(request.project).resolve(request.source), animation_graph_byte_limit);
    Snapshot result{AnimationGraphSource::parse(bytes), {}};
    require(result.source.asset() == request.asset,
            "Animation Graph source/catalog identity mismatch");
    const auto graph = result.source.compiled();
    const auto catalog = AssetCatalog::open_project(request.project);
    const auto skeleton = catalog.resolve(graph.skeleton, SkeletonAsset::type);
    require(skeleton.state == AssetState::Available && skeleton.record->subasset.has_value(),
            "Animation Graph needs an imported model skeleton");
    const auto selected = asset_detail::load_model_selection(
        request.project, catalog, skeleton.record->subasset->owner, stop);
    const auto joints = asset_detail::model_skeleton_joints(selected, graph.skeleton);
    require(!joints.assets.empty() && joints.assets.size() <= animation_graph_joint_limit,
            "Animation Graph supports skeletons with 1–256 joints");
    auto& input = result.input;
    input.source_digest = asset_detail::content_digest(bytes);
    input.importer = importer_id;
    input.importer_revision = recipe;
    input.output_format = "forge.animation-graph";
    input.output_version = 1;
    input.platform = request.target.platform;
    input.backend = request.target.backend;
    input.profile = request.target.profile;
    input.dependencies.push_back({graph.skeleton, SkeletonAsset::type, AssetDependencyKind::Runtime,
                                  "graph.skeleton", selected.revision});
    std::map<AssetId, std::string> masks;
    for (const auto& node : graph.nodes) {
        require(!stop.stop_requested(), "Animation Graph import cancelled");
        if (node.kind == AnimationGraphNodeKind::Clip) {
            const auto binding =
                animation_detail::select_animation_clip(catalog, {graph.skeleton}, node.clip);
            require(binding.skeleton_revision == selected.revision,
                    "Animation Graph clips select a different skeleton revision");
            input.dependencies.push_back({node.clip.id, AnimationClipAsset::type,
                                          AssetDependencyKind::Runtime,
                                          "graph.clip:" + node.id.str(), binding.clip_revision});
        } else if (node.mask.id) {
            if (!masks.contains(node.mask.id)) {
                const auto resolved = catalog.resolve(node.mask);
                require(resolved.state == AssetState::Available && !resolved.record->subasset,
                        "Animation Graph bone mask is unavailable or has the wrong ownership");
                const auto revision =
                    animation_mask_selection(*resolved.record).at("key").get<std::string>();
                auto cache = DerivedDataCache::open_existing(
                    request.project, {animation_mask_cooked_byte_limit,
                                      animation_mask_cooked_byte_limit + 65536, 1});
                (void)cache.load_selected(revision, [&](const auto& artifact) {
                    const auto mask = admit_animation_mask(artifact, *resolved.record);
                    require(mask.skeleton_revision == selected.revision,
                            "Animation Graph mask selects a different skeleton revision");
                    (void)mask.mask.weights(graph.skeleton, joints.assets, joints.parents);
                });
                masks.emplace(node.mask.id, revision);
            }
            input.dependencies.push_back({node.mask.id, AnimationMaskAsset::type,
                                          AssetDependencyKind::Runtime,
                                          "graph.mask:" + node.id.str(), masks.at(node.mask.id)});
        }
    }
    require(!stop.stop_requested(), "Animation Graph import cancelled");
    return result;
}
const ArtifactFile& graph_file(const std::vector<ArtifactFile>& files) {
    require(files.size() == 1 && files.front().name == "graph.json",
            "Animation Graph artifact requires one cooked graph");
    (void)AnimationGraphSource::parse_cooked(files.front().bytes);
    return files.front();
}
class AnimationGraphImporter final : public AssetImporter {
  public:
    AnimationGraphImporter()
        : AssetImporter(
              [] {
                  AssetImporterDescriptor d;
                  d.id = importer_id;
                  d.revision = recipe;
                  d.label = "Animation Graph";
                  d.description = "Cook a bounded skeletal clip, blend and additive graph.";
                  d.extensions = {".json"};
                  d.source_kinds = {"forge.animation-graph"};
                  d.output_types = {AnimationGraphAsset::type};
                  d.output_format = "forge.animation-graph";
                  d.output_version = 1;
                  d.targets = {{"windows", "d3d12", "desktop"}, {"linux", "none", "cpu"}};
                  d.execution = ImportExecution::TrustedCpuTask;
                  d.limits.output_files = 1;
                  d.limits.output_bytes = animation_graph_cooked_byte_limit;
                  return d;
              }(),
              {importer_id, 1, {}}) {}
    ImportProbeResult probe(const ImportProbe& source) const override {
        return path_utf8(source.source).ends_with(AnimationGraphDocumentTraits::suffix)
                   ? ImportProbeResult{ImportProbeMatch::Possible, "forge.animation-graph",
                                       "Animation Graph source suffix"}
                   : ImportProbeResult{};
    }
    AssetImportPlan discover(const AssetImportRequest& request,
                             std::stop_token stop) const override {
        require(std::find(descriptor().targets.begin(), descriptor().targets.end(),
                          request.target) != descriptor().targets.end(),
                "Unsupported Animation Graph target");
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
                "Animation Graph source or dependencies changed after discovery");
        if (progress)
            progress(.5, "Preparing animation graph dependency order");
        auto cooked = snapshot.source.cook();
        require(!stop.stop_requested(), "Animation Graph import cancelled");
        return {{"graph.json", std::move(cooked)}};
    }
    void validate(const CachedArtifact& artifact) const override {
        (void)graph_file(artifact.files);
    }
};
} // namespace
std::shared_ptr<const AssetImporterRegistry> animation_graph_import_registry() {
    auto registry = std::make_shared<AssetImporterRegistry>();
    registry->add(std::make_shared<AnimationGraphImporter>());
    registry->seal();
    return registry;
}
void prepare_animation_graph_publication(AssetPublicationCandidate& candidate,
                                         const AssetImportPlan& plan) {
    require(plan.input.importer == importer_id &&
                plan.input.output_format == "forge.animation-graph" &&
                plan.input.output_version == 1 &&
                plan.data.at("asset").get<AssetId>() == candidate.ticket.owner &&
                candidate.sidecar.identity.entries.empty(),
            "Animation Graph publication identity/family mismatch");
    const auto& file = graph_file(candidate.files);
    (void)decode_animation_graph(file.bytes, candidate.ticket.owner);
    auto& identity = candidate.sidecar.identity;
    identity.owner = candidate.ticket.owner;
    identity.source = candidate.ticket.source;
    identity.source_digest = candidate.input.source_digest;
    identity.evidence_schema = "forge.animation-graph.single.v1";
    candidate.records = {{candidate.ticket.owner,
                          AnimationGraphAsset::type,
                          candidate.ticket.source,
                          1,
                          {},
                          {{"forge.animation_graph",
                            {{"version", 1},
                             {"file", file.name},
                             {"sha256", asset_detail::content_digest(file.bytes)},
                             {"bytes", file.bytes.size()}}}}}};
}
} // namespace forge
