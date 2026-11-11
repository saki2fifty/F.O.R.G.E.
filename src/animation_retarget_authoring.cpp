#include "animation_retarget_authoring.hpp"
#include "animation_clip_selection.hpp"
#include "model_skeleton_binding.hpp"
#include <algorithm>

namespace forge {
namespace {
using namespace animation_detail;
using namespace asset_detail;
constexpr std::uint64_t model_byte_limit = 64 * 1024 * 1024;
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
struct Snapshot {
    AnimationRetargetSource source;
    AnimationRetargetSettings settings;
    AssetBuildInput input;
    std::shared_ptr<const ModelSelection> source_model, target_model;
    std::shared_ptr<const Skeleton> source_skeleton, target_skeleton;
    std::shared_ptr<const Clip> source_clip;
};
RetargetRig rig(const ModelSelection& model, AssetId id, const Skeleton& skeleton) {
    const auto joints = model_skeleton_joints(model, id);
    RetargetRig result{joints.assets, joints.parents, {}};
    require(skeleton.info().tracks <= animation_retarget_joint_limit &&
                skeleton.info().tracks == joints.assets.size(),
            "Retarget skeleton exceeds 256 joints or differs from its durable joint binding");
    for (std::size_t i = 0; i < joints.assets.size(); ++i)
        result.rest.push_back(skeleton.joint_rest_pose(i));
    return result;
}
Snapshot capture(const AssetImportRequest& request, std::stop_token stop) {
    require(!stop.stop_requested(), "Animation retarget import cancelled");
    const auto bytes = read_bytes(ProjectPaths(request.project).resolve(request.source),
                                  animation_retarget_byte_limit);
    Snapshot result;
    result.source = AnimationRetargetSource::parse(bytes);
    require(result.source.asset() == request.asset, "Retarget source/catalog identity mismatch");
    result.settings = result.source.settings();
    const auto& settings = result.settings;
    const auto catalog = AssetCatalog::open_project(request.project);
    const auto source_binding =
        select_animation_clip(catalog, {settings.source_skeleton}, {settings.source_clip});
    require(
        !source_binding.baked,
        "Retarget source must be an imported model clip; recursive baked inputs are unsupported");
    const auto target = catalog.resolve(settings.target_skeleton, SkeletonAsset::type);
    require(target.state == AssetState::Available && target.record->subasset,
            "Retarget target must be a published imported model Skeleton");
    result.source_model = std::make_shared<const ModelSelection>(load_model_selection(
        request.project, catalog, source_binding.model, stop, model_byte_limit));
    result.target_model = source_binding.model == target.record->subasset->owner
                              ? result.source_model
                              : std::make_shared<const ModelSelection>(load_model_selection(
                                    request.project, catalog, target.record->subasset->owner, stop,
                                    model_byte_limit));
    const auto& source = *result.source_model;
    const auto& destination = *result.target_model;
    const auto& target_member = destination.member(settings.target_skeleton);
    require(target_member.identity.type == SkeletonAsset::type,
            "Retarget target selection is not a skeleton");
    result.source_skeleton =
        std::make_shared<Skeleton>(source.bytes(source.member(settings.source_skeleton)));
    result.target_skeleton = settings.source_skeleton == settings.target_skeleton
                                 ? result.source_skeleton
                                 : std::make_shared<Skeleton>(destination.bytes(target_member));
    result.source_clip = std::make_shared<Clip>(source.bytes(source.member(settings.source_clip)));
    require(result.source_clip->info().duration <= 60 &&
                result.source_clip->info().tracks == result.source_skeleton->info().tracks,
            "Retarget source needs a matching imported clip no longer than 60 seconds");
    require(result.source_clip->info().unit_scales,
            "Retarget source must use unit scales throughout the clip");
    (void)RetargetPose(rig(source, settings.source_skeleton, *result.source_skeleton),
                       rig(destination, settings.target_skeleton, *result.target_skeleton),
                       settings);
    (void)retarget_sample_intervals(result.source_clip->info().duration, settings.sampling_rate,
                                    result.source_skeleton->info().tracks,
                                    result.target_skeleton->info().tracks);
    auto& input = result.input;
    input.source_digest = content_digest(bytes);
    input.importer = retarget_importer;
    input.importer_revision = retarget_recipe;
    input.output_format = baked_clip_format;
    input.output_version = 1;
    input.platform = request.target.platform;
    input.backend = request.target.backend;
    input.profile = request.target.profile;
    input.dependencies = {{settings.source_skeleton, SkeletonAsset::type,
                           AssetDependencyKind::Build, "source.skeleton", source.revision},
                          {settings.source_clip, AnimationClipAsset::type,
                           AssetDependencyKind::Build, "source.clip", source.revision},
                          {settings.target_skeleton, SkeletonAsset::type,
                           AssetDependencyKind::Runtime, "skeleton", destination.revision}};
    require(!stop.stop_requested(), "Animation retarget import cancelled");
    return result;
}
class AnimationRetargetImporter final : public AssetImporter {
  public:
    AnimationRetargetImporter()
        : AssetImporter(
              [] {
                  AssetImporterDescriptor d;
                  d.id = retarget_importer;
                  d.revision = retarget_recipe;
                  d.label = "Animation retarget";
                  d.description = "Bake mapped joint rotations for an exact target skeleton.";
                  d.extensions = {".json"};
                  d.source_kinds = {"forge.animation-retarget"};
                  d.output_types = {AnimationClipAsset::type};
                  d.output_format = baked_clip_format;
                  d.output_version = 1;
                  d.targets = {{"windows", "d3d12", "desktop"}, {"linux", "none", "cpu"}};
                  d.execution = ImportExecution::TrustedCpuTask;
                  d.limits.output_files = 2;
                  d.limits.output_bytes = max_archive_bytes + baked_clip_descriptor_limit;
                  return d;
              }(),
              {retarget_importer, 1, {}}) {}
    ImportProbeResult probe(const ImportProbe& source) const override {
        return path_utf8(source.source).ends_with(AnimationRetargetDocumentTraits::suffix)
                   ? ImportProbeResult{ImportProbeMatch::Possible, "forge.animation-retarget",
                                       "Animation retarget source suffix"}
                   : ImportProbeResult{};
    }
    AssetImportPlan discover(const AssetImportRequest& request,
                             std::stop_token stop) const override {
        require(std::find(descriptor().targets.begin(), descriptor().targets.end(),
                          request.target) != descriptor().targets.end(),
                "Unsupported animation retarget target");
        (void)settings().effective(request.settings);
        const auto snapshot = capture(request, stop);
        return {snapshot.input,
                {},
                {{"asset", request.asset},
                 {"skeleton", snapshot.settings.target_skeleton},
                 {"model", snapshot.target_model->owner},
                 {"skeleton_revision", snapshot.target_model->revision}}};
    }
    std::vector<ArtifactFile>
    import_and_cook(const AssetImportRequest& request, const AssetImportPlan& plan,
                    std::stop_token stop,
                    const std::function<void(double, std::string)>& progress) const override {
        const auto snapshot = capture(request, stop);
        require(snapshot.input.document() == plan.input.document(),
                "Retarget source or skeleton/clip selection changed after discovery");
        const auto& settings = snapshot.settings;
        RetargetPose retarget(
            rig(*snapshot.source_model, settings.source_skeleton, *snapshot.source_skeleton),
            rig(*snapshot.target_model, settings.target_skeleton, *snapshot.target_skeleton),
            settings);
        Sampler sampler(snapshot.source_skeleton, snapshot.source_clip);
        const float duration = snapshot.source_clip->info().duration;
        const auto intervals = retarget_sample_intervals(duration, settings.sampling_rate,
                                                         snapshot.source_skeleton->info().tracks,
                                                         snapshot.target_skeleton->info().tracks);
        SampledAnimation samples;
        samples.times.reserve(intervals + 1);
        samples.poses.reserve(intervals + 1);
        for (unsigned i = 0; i <= intervals; ++i) {
            require(!stop.stop_requested(), "Animation retarget import cancelled");
            const float ratio = float(double(i) / intervals);
            (void)sampler.sample(ratio);
            samples.times.push_back(i == intervals ? duration : duration * ratio);
            samples.poses.push_back(retarget.sample(sampler.local_pose(), stop));
        }
        if (progress)
            progress(.6, "Compressing retargeted joint samples");
        auto archive = build_sampled_animation(samples, stop);
        nlohmann::json descriptor{
            {"format", baked_clip_format},
            {"version", 1},
            {"asset_id", request.asset},
            {"skeleton", settings.target_skeleton},
            {"model", snapshot.target_model->owner},
            {"skeleton_revision", snapshot.target_model->revision},
            {"joint_nodes",
             snapshot.target_model->index.hierarchy.at("animation").at("plan").at("joint_nodes")}};
        const auto text = descriptor.dump();
        const auto bytes = std::as_bytes(std::span(text));
        std::vector<ArtifactFile> files{{"clip.json", {bytes.begin(), bytes.end()}},
                                        {"clip.ozz", std::move(archive)}};
        const auto cooked = decode_baked_animation_clip(files);
        Sampler target(snapshot.target_skeleton, cooked.native);
        for (const auto ratio : {0.f, .5f, 1.f})
            (void)target.sample(ratio);
        require(!stop.stop_requested(), "Animation retarget import cancelled");
        return files;
    }
    void validate(const CachedArtifact& artifact) const override {
        (void)decode_baked_animation_clip(artifact.files);
    }
};
} // namespace
std::shared_ptr<const AssetImporterRegistry> animation_retarget_import_registry() {
    auto registry = std::make_shared<AssetImporterRegistry>();
    registry->add(std::make_shared<AnimationRetargetImporter>());
    registry->seal();
    return registry;
}
void prepare_animation_retarget_publication(AssetPublicationCandidate& candidate,
                                            const AssetImportPlan& plan) {
    require(plan.input.importer == retarget_importer &&
                plan.data.at("asset").get<AssetId>() == candidate.ticket.owner &&
                candidate.sidecar.identity.entries.empty(),
            "Retarget publication identity/family mismatch");
    const auto clip = decode_baked_animation_clip(candidate.files);
    require(clip.asset == candidate.ticket.owner &&
                clip.skeleton == plan.data.at("skeleton").get<AssetId>() &&
                clip.model == plan.data.at("model").get<AssetId>() &&
                Json(clip.skeleton_revision) == plan.data.at("skeleton_revision"),
            "Retarget candidate target binding differs from discovery");
    auto& identity = candidate.sidecar.identity;
    identity.owner = candidate.ticket.owner;
    identity.source = candidate.ticket.source;
    identity.source_digest = candidate.input.source_digest;
    identity.evidence_schema = "forge.animation-retarget.single.v1";
    nlohmann::json files = nlohmann::json::object();
    for (const auto& file : candidate.files)
        files[file.name] = {{"sha256", content_digest(file.bytes)}, {"bytes", file.bytes.size()}};
    candidate.records = {{candidate.ticket.owner,
                          AnimationClipAsset::type,
                          candidate.ticket.source,
                          1,
                          {},
                          {{"forge.animation_clip",
                            {{"version", 1},
                             {"skeleton", clip.skeleton},
                             {"model", clip.model},
                             {"skeleton_revision", clip.skeleton_revision},
                             {"files", std::move(files)}}}}}};
}
} // namespace forge
