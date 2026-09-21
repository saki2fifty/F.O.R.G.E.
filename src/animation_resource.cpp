#include "animation_resource.hpp"
#include "model_selection.hpp"
#include <forge/model_asset.hpp>
namespace forge::animation_detail {
namespace {
void require(bool ok, const char* why) {
    if (!ok)
        throw ArchiveError(why);
}
const AssetRecord& member(const AssetCatalog& catalog, AssetId id, const char* type) {
    const auto found = catalog.records().find(id);
    require(found != catalog.records().end() && found->second.type == type &&
                found->second.subasset && !found->second.subasset->removed,
            "Model animation member is unavailable or has the wrong type");
    return found->second;
}
void selected(const asset_detail::ModelSelection& selection, const std::string& revision,
              std::uint64_t generation) {
    require(selection.revision == revision && selection.generation == generation,
            "Model animation resource selection changed");
}
void pending_or_failed(const ResourceTicket& ticket) {
    const auto info = ticket.inspect();
    if (info.state != ResourceState::Ready && resource_detail::terminal(info.state))
        throw ArchiveError("Animation resource " + info.identity.asset.str() + ": " +
                           resource_state_name(info.state) + " / " + info.diagnostic);
}
} // namespace
std::size_t SkeletonResourceData::resident_bytes() const {
    return sizeof(*this) + native->resident_bytes() + joint_nodes.capacity() * sizeof(std::size_t);
}
std::size_t ClipResourceData::resident_bytes() const {
    return sizeof(*this) + native->resident_bytes() + morphs->resident_bytes() +
           transform_channels.capacity() * sizeof(AnimatedTransformChannel);
}
ModelAnimationResources::ModelAnimationResources(std::filesystem::path project)
    : project_(std::move(project)), skeletons_({1, 64, 64, 64 * 1024 * 1024}),
      clips_({1, 64, 64, 64 * 1024 * 1024}) {}
ModelAnimationRequest ModelAnimationResources::request(std::shared_ptr<const AssetCatalog> catalog,
                                                       AssetRef<SkeletonAsset> skeleton,
                                                       AssetRef<AnimationClipAsset> clip) {
    require(bool(catalog), "Model animation requires a selected catalog");
    const auto& s = member(*catalog, skeleton.id, SkeletonAsset::type);
    const auto& c = member(*catalog, clip.id, AnimationClipAsset::type);
    require(s.subasset->owner == c.subasset->owner, "Skeleton and clip belong to different models");
    const auto model = s.subasset->owner;
    const auto root = catalog->records().find(model);
    require(root != catalog->records().end() && root->second.type == ModelAsset::type &&
                !root->second.subasset,
            "Animation model owner is unavailable");
    const auto& imported = root->second.metadata.at("forge.import");
    require(s.metadata.at("forge.import") == imported && c.metadata.at("forge.import") == imported,
            "Animation members mix model publication revisions");
    const auto revision = imported.at("key").get<std::string>();
    const auto& serial = imported.at("generation");
    require(serial.is_number_unsigned() && serial.get<std::uint64_t>() > 0,
            "Invalid model animation publication generation");
    const auto generation = serial.get<std::uint64_t>();
    require(c.dependency_edges.size() == 1 &&
                c.dependency_edges[0].kind == AssetDependencyKind::Runtime &&
                c.dependency_edges[0].role == "skeleton" &&
                c.dependency_edges[0].target == skeleton.id &&
                c.dependency_edges[0].expected_type == SkeletonAsset::type &&
                c.dependency_edges[0].revision == revision,
            "Animation clip is bound to a different skeleton revision");
    auto project = project_;
    auto skeleton_ticket = skeletons_.request(
        skeleton, revision, generation,
        [project, catalog, model, revision, generation, skeleton](std::stop_token stop) {
            const auto selection =
                asset_detail::load_model_selection(project, *catalog, model, stop);
            selected(selection, revision, generation);
            auto data = std::make_unique<SkeletonResourceData>();
            data->native =
                std::make_shared<Skeleton>(selection.bytes(selection.member(skeleton.id)));
            data->model = model;
            data->joint_nodes = selection.index.hierarchy.at("animation")
                                    .at("plan")
                                    .at("joint_nodes")
                                    .get<std::vector<std::size_t>>();
            require(!stop.stop_requested(), "Skeleton load cancelled");
            const auto bytes = data->resident_bytes();
            return ResourceCandidate<SkeletonAsset>{std::move(data), {0, 0, 0, 0, bytes}};
        });
    auto clip_ticket = clips_.request(
        clip, revision, generation,
        [project, catalog, model, revision, generation, clip, skeleton](std::stop_token stop) {
            const auto selection =
                asset_detail::load_model_selection(project, *catalog, model, stop);
            selected(selection, revision, generation);
            const auto& clip_member = selection.member(clip.id);
            require(selection.bindings.at(clip_member.bindings.at("skeleton")) == skeleton.id,
                    "Cooked animation skeleton binding differs");
            auto data = std::make_unique<ClipResourceData>();
            data->native = std::make_shared<Clip>(selection.bytes(clip_member));
            data->model = model;
            data->skeleton = skeleton.id;
            const auto& plan = selection.index.hierarchy.at("animation").at("plan");
            for (const auto& entry : plan.at("clips"))
                if (entry.at("file") == clip_member.artifact.file) {
                    data->morphs = std::make_unique<const asset_detail::MorphAnimation>(
                        entry.at("morph_tracks"));
                    data->has_transform_channels = plan.at("version") == 2;
                    if (data->has_transform_channels)
                        for (const auto& channel : entry.at("transform_channels")) {
                            const auto path = channel.at("path").get<std::string>();
                            data->transform_channels.push_back(
                                {channel.at("node").get<std::size_t>(),
                                 path == "translation" ? AnimatedTransformPath::Translation
                                 : path == "rotation"  ? AnimatedTransformPath::Rotation
                                                       : AnimatedTransformPath::Scale});
                        }
                }
            require(bool(data->morphs) && !stop.stop_requested(),
                    "Clip morph plan missing or load cancelled");
            const auto bytes = data->resident_bytes();
            return ResourceCandidate<AnimationClipAsset>{std::move(data), {0, 0, 0, 0, bytes}};
        },
        {skeleton_ticket});
    return {skeleton, clip,      std::move(skeleton_ticket), std::move(clip_ticket), model,
            revision, generation};
}
void ModelAnimationResources::pump() {
    skeletons_.pump();
    clips_.pump();
}
ModelAnimationLease ModelAnimationResources::acquire(const ModelAnimationRequest& r) {
    pending_or_failed(r.skeleton_ticket);
    pending_or_failed(r.clip_ticket);
    auto s = skeletons_.acquire(r.skeleton_ticket);
    auto c = clips_.acquire(r.clip_ticket);
    if (!s || !c)
        return {};
    require(s.identity().revision == r.revision && c.identity().revision == r.revision &&
                s->model == r.model && c->model == r.model && c->skeleton == r.skeleton.id &&
                s->native->info().tracks == c->native->info().tracks,
            "Animation resources are not one compatible model revision");
    return {std::move(s), std::move(c)};
}
bool ModelAnimationResources::prepare_recovery(const ModelAnimationRequest& r,
                                               std::chrono::milliseconds timeout) {
    if (timeout < std::chrono::milliseconds::zero())
        return false;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    if (!skeletons_.wait(r.skeleton_ticket, timeout))
        return false;
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    return remaining >= std::chrono::milliseconds::zero() && clips_.wait(r.clip_ticket, remaining);
}
void ModelAnimationResources::unload(const ModelAnimationRequest& r) {
    clips_.unload(r.clip);
    skeletons_.unload(r.skeleton);
}
void ModelAnimationResources::unload_skeleton(AssetRef<SkeletonAsset> id) { skeletons_.unload(id); }
void ModelAnimationResources::unload_clip(AssetRef<AnimationClipAsset> id) { clips_.unload(id); }
void ModelAnimationResources::close() {
    clips_.close();
    skeletons_.close();
}
ResourceStatistics ModelAnimationResources::skeleton_statistics() const {
    return skeletons_.statistics();
}
ResourceStatistics ModelAnimationResources::clip_statistics() const { return clips_.statistics(); }
} // namespace forge::animation_detail
