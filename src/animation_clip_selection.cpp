#include "animation_clip_selection.hpp"
#include "asset_bytes.hpp"
#include "bounded_json.hpp"
#include "model_skeleton_binding.hpp"
#include <algorithm>
#include <set>

namespace forge::animation_detail {
namespace {
void require(bool ok, const char* why) {
    if (!ok)
        throw ArchiveError(why);
}
const AssetRecord& record(const AssetCatalog& catalog, AssetId id, const char* type) {
    const auto found = catalog.records().find(id);
    require(found != catalog.records().end() && found->second.type == type &&
                (!found->second.subasset || !found->second.subasset->removed),
            "Animation asset is missing, removed or has the wrong type");
    return found->second;
}
const nlohmann::json& baked_selection(const AssetRecord& clip) {
    require(clip.type == AnimationClipAsset::type && !clip.subasset,
            "Baked animation clip must be a root AnimationClip asset");
    const auto& selected = clip.metadata.at("forge.import");
    require(selected.at("version") == 1 && selected.at("importer") == retarget_importer &&
                selected.at("importer_revision") == retarget_recipe &&
                selected.at("output_format") == baked_clip_format &&
                selected.at("output_version") == 1 &&
                valid_content_digest(selected.at("key").get<std::string>()) &&
                selected.at("generation").is_number_unsigned() &&
                selected.at("generation").get<std::uint64_t>() > 0,
            "Unsupported baked animation clip selection");
    return selected;
}
std::vector<AssetDependency> runtime_edges(std::span<const AssetDependency> edges) {
    std::vector<AssetDependency> result;
    for (const auto& edge : edges)
        if (edge.kind == AssetDependencyKind::Runtime && !is_declared_runtime_dependency(edge))
            result.push_back(edge);
    std::sort(result.begin(), result.end());
    return result;
}
} // namespace
AnimationClipSelection select_animation_clip(const AssetCatalog& catalog,
                                             AssetRef<SkeletonAsset> skeleton,
                                             AssetRef<AnimationClipAsset> clip) {
    const auto& s = record(catalog, skeleton.id, SkeletonAsset::type);
    const auto& c = record(catalog, clip.id, AnimationClipAsset::type);
    require(s.subasset.has_value(), "Animation needs an imported model skeleton");
    const auto& model = record(catalog, s.subasset->owner, ModelAsset::type);
    require(!model.subasset, "Animation model owner must be a root asset");
    const auto& imported = model.metadata.at("forge.import");
    require(s.metadata.at("forge.import") == imported,
            "Animation skeleton mixes model publication revisions");
    AnimationClipSelection result;
    result.model = model.id;
    result.skeleton_revision = imported.at("key").get<std::string>();
    require(valid_content_digest(result.skeleton_revision) &&
                imported.at("generation").is_number_unsigned(),
            "Invalid animation skeleton publication");
    result.skeleton_generation = imported.at("generation").get<std::uint64_t>();
    require(result.skeleton_generation > 0, "Invalid animation skeleton generation");
    result.baked = !c.subasset;
    if (c.subasset) {
        require(c.subasset->owner == model.id && c.metadata.at("forge.import") == imported,
                "Skeleton and imported clip must belong to one exact model revision");
        result.clip_revision = result.skeleton_revision;
        result.clip_generation = result.skeleton_generation;
        require(c.dependency_edges.size() == 1,
                "Imported animation clip skeleton dependency is invalid");
    } else {
        const auto& selected = baked_selection(c);
        const auto& meta = c.metadata.at("forge.animation_clip");
        require(meta.at("version") == 1 && meta.at("skeleton").get<AssetId>() == skeleton.id &&
                    meta.at("model").get<AssetId>() == result.model &&
                    meta.at("skeleton_revision") == result.skeleton_revision,
                "Baked animation clip is bound to another target skeleton revision");
        result.clip_revision = selected.at("key").get<std::string>();
        result.clip_generation = selected.at("generation").get<std::uint64_t>();
    }
    const std::vector<AssetDependency> expected{{skeleton.id, SkeletonAsset::type,
                                                 AssetDependencyKind::Runtime, "skeleton",
                                                 result.skeleton_revision}};
    require(runtime_edges(c.dependency_edges) == expected,
            "Animation clip is bound to a different skeleton revision");
    return result;
}
std::set<AssetId> compatible_animation_clips(const AssetCatalog& catalog,
                                             AssetRef<SkeletonAsset> skeleton) {
    std::set<AssetId> result;
    if (!skeleton.id)
        return result;
    for (const auto& [id, value] : catalog.records()) {
        if (value.type != AnimationClipAsset::type)
            continue;
        try {
            (void)select_animation_clip(catalog, skeleton, {id});
            result.insert(id);
        } catch (const std::exception&) {
            // Picker filtering only. Selecting or publishing still reports the
            // shared binding admission error rather than guessing a replacement.
        }
    }
    return result;
}
AnimationSkeletonInspection inspect_animation_skeleton(const std::filesystem::path& project,
                                                       const AssetCatalog& catalog,
                                                       AssetRef<SkeletonAsset> skeleton,
                                                       std::stop_token stop) {
    const auto& selected = record(catalog, skeleton.id, SkeletonAsset::type);
    require(selected.subasset.has_value(), "Choose a published imported model Skeleton");
    const auto model = asset_detail::load_model_selection(
        project, catalog, selected.subasset->owner, stop, 64 * 1024 * 1024);
    const auto joints = asset_detail::model_skeleton_joints(model, skeleton.id);
    require(!joints.assets.empty() && joints.assets.size() <= 256,
            "Retarget inspection supports 1 to 256 joints");
    Skeleton native(model.bytes(model.member(skeleton.id)));
    require(native.info().parents == joints.parents && native.info().tracks == joints.assets.size(),
            "Skeleton archive differs from durable joint metadata");
    AnimationSkeletonInspection result{
        skeleton.id, model.owner, model.revision, model.generation, joints.assets, joints.parents,
        {},          {}};
    for (std::size_t i = 0; i < joints.assets.size(); ++i) {
        require(!stop.stop_requested(), "Skeleton inspection cancelled");
        result.rest.push_back(native.joint_rest_pose(i));
        result.labels.push_back(model.member(joints.assets[i]).identity.display_name);
    }
    return result;
}
BakedAnimationClip decode_baked_animation_clip(std::span<const ArtifactFile> files) {
    require(files.size() == 2, "Baked animation clip needs a descriptor and native archive");
    const ArtifactFile* descriptor = nullptr;
    const ArtifactFile* archive = nullptr;
    for (const auto& file : files) {
        if (file.name == "clip.json" && !descriptor)
            descriptor = &file;
        else if (file.name == "clip.ozz" && !archive)
            archive = &file;
        else
            throw ArchiveError("Unexpected or duplicate baked animation file");
    }
    require(descriptor && archive, "Baked animation file is missing");
    const auto d =
        asset_detail::parse_bounded_json(descriptor->bytes, baked_clip_descriptor_limit, 4096, 8);
    require(d.at("format") == baked_clip_format && d.at("version") == 1,
            "Unsupported baked animation descriptor");
    BakedAnimationClip value;
    value.asset = d.at("asset_id").get<AssetId>();
    value.skeleton = d.at("skeleton").get<AssetId>();
    value.model = d.at("model").get<AssetId>();
    value.skeleton_revision = d.at("skeleton_revision").get<std::string>();
    require(value.asset && value.skeleton && value.model &&
                valid_content_digest(value.skeleton_revision) && d.at("joint_nodes").is_array() &&
                !d.at("joint_nodes").empty() && d.at("joint_nodes").size() <= 256,
            "Invalid baked animation binding/counts");
    std::set<std::size_t> nodes;
    for (const auto& entry : d.at("joint_nodes")) {
        require(entry.is_number_integer() && entry >= 0 && entry < 100000,
                "Baked animation joint node is outside model bounds");
        const auto node = entry.get<std::size_t>();
        require(nodes.insert(node).second, "Baked animation joint nodes are duplicated");
        value.joint_nodes.push_back(node);
    }
    value.native = std::make_shared<Clip>(archive->bytes);
    require(value.native->info().tracks == value.joint_nodes.size() &&
                value.native->info().unit_scales && value.native->info().duration <= 60,
            "Baked animation tracks differ from its target joint binding");
    return value;
}
BakedAnimationClip admit_baked_animation_clip(const CachedArtifact& artifact,
                                              const AssetRecord& clip) {
    const auto& selected = baked_selection(clip);
    require(selected.at("key") == artifact.key,
            "Baked animation artifact differs from its selected revision");
    auto value = decode_baked_animation_clip(artifact.files);
    const auto& input = artifact.manifest.at("inputs");
    for (const auto* field : {"importer", "importer_revision", "output_format", "output_version",
                              "platform", "backend", "profile"})
        require(input.at(field) == selected.at(field), "Baked animation recipe mismatch");
    const auto& metadata = clip.metadata.at("forge.animation_clip");
    require(value.asset == clip.id && metadata.at("version") == 1 &&
                metadata.at("skeleton").get<AssetId>() == value.skeleton &&
                metadata.at("model").get<AssetId>() == value.model &&
                metadata.at("skeleton_revision") == value.skeleton_revision &&
                input.at("source") == selected.at("source_digest") &&
                selected.at("artifact_digest") == asset_build_digest(artifact.manifest.at("files")),
            "Baked animation artifact/provenance mismatch");
    for (const auto& file : artifact.files) {
        const auto& fact = metadata.at("files").at(file.name);
        require(fact.at("sha256") == asset_detail::content_digest(file.bytes) &&
                    fact.at("bytes") == file.bytes.size(),
                "Baked animation file facts differ from immutable data");
    }
    require(metadata.at("files").size() == artifact.files.size(),
            "Baked animation metadata contains unexpected file facts");
    const auto dependencies = input.at("dependencies").get<std::vector<AssetDependency>>();
    require(dependencies.size() == 3, "Baked animation build dependency closure is incomplete");
    std::set<std::string> roles;
    for (const auto& edge : dependencies) {
        require(
            edge.target && valid_content_digest(edge.revision) && roles.insert(edge.role).second &&
                ((edge.role == "source.skeleton" && edge.expected_type == SkeletonAsset::type &&
                  edge.kind == AssetDependencyKind::Build) ||
                 (edge.role == "source.clip" && edge.expected_type == AnimationClipAsset::type &&
                  edge.kind == AssetDependencyKind::Build) ||
                 (edge.role == "skeleton" && edge.expected_type == SkeletonAsset::type &&
                  edge.kind == AssetDependencyKind::Runtime)),
            "Baked animation build dependency contract is invalid");
    }
    require(runtime_edges(dependencies) ==
                    std::vector<AssetDependency>{{value.skeleton, SkeletonAsset::type,
                                                  AssetDependencyKind::Runtime, "skeleton",
                                                  value.skeleton_revision}} &&
                runtime_edges(dependencies) == runtime_edges(clip.dependency_edges),
            "Baked animation target dependency differs from its descriptor");
    auto authored_edges = clip.dependency_edges;
    std::erase_if(authored_edges, is_declared_runtime_dependency);
    if (std::any_of(authored_edges.begin(), authored_edges.end(),
                    [](const auto& edge) { return edge.kind != AssetDependencyKind::Runtime; })) {
        auto ordered = dependencies;
        std::sort(ordered.begin(), ordered.end());
        std::sort(authored_edges.begin(), authored_edges.end());
        require(ordered == authored_edges, "Baked animation authored build dependencies differ");
    }
    require(selected.at("dependency_digest") ==
                asset_build_digest(nlohmann::json{{"assets", input.at("dependencies")},
                                                  {"sources", input.at("source_dependencies")}}),
            "Baked animation dependency provenance mismatch");
    return value;
}
BakedAnimationClip load_baked_animation_clip(const std::filesystem::path& project,
                                             const AssetRecord& clip,
                                             const asset_detail::ModelSelection& target,
                                             std::stop_token stop) {
    require(!stop.stop_requested(), "Baked animation load cancelled");
    const auto& selected = baked_selection(clip);
    auto cache = DerivedDataCache::open_existing(
        project, {max_archive_bytes, max_archive_bytes + baked_clip_descriptor_limit, 2});
    std::optional<BakedAnimationClip> value;
    (void)cache.load_selected(selected.at("key").get<std::string>(), [&](const auto& artifact) {
        value = admit_baked_animation_clip(artifact, clip);
    });
    require(value && value->model == target.owner && value->skeleton_revision == target.revision &&
                target.member(value->skeleton).identity.type == SkeletonAsset::type &&
                value->joint_nodes == target.index.hierarchy.at("animation")
                                          .at("plan")
                                          .at("joint_nodes")
                                          .get<std::vector<std::size_t>>() &&
                !stop.stop_requested(),
            "Baked animation target binding changed or load was cancelled");
    return std::move(*value);
}
} // namespace forge::animation_detail
