#include "animation_resource.hpp"
#include "animation_clip_selection.hpp"
#include "model_selection.hpp"
#include <forge/model_asset.hpp>
namespace forge::animation_detail {
namespace {
void require(bool ok, const char* why) {
    if (!ok)
        throw ArchiveError(why);
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
    return sizeof(*this) + native->resident_bytes() + joint_nodes.capacity() * sizeof(std::size_t) +
           joint_assets.capacity() * sizeof(AssetId);
}
std::size_t ClipResourceData::resident_bytes() const {
    auto bytes = sizeof(*this) + native->resident_bytes() + morphs->resident_bytes() +
                 transform_channels.capacity() * sizeof(AnimatedTransformChannel) +
                 morph_defaults.capacity() * sizeof(asset_detail::MorphWeightsSample);
    for (const auto& entry : morph_defaults)
        bytes += entry.weights.capacity() * sizeof(float);
    return bytes;
}
ModelAnimationResources::ModelAnimationResources(std::filesystem::path project)
    : project_(std::move(project)), skeletons_({1, 64, 64, 64 * 1024 * 1024}),
      clips_({1, 64, 64, 64 * 1024 * 1024}) {}
ModelAnimationRequest ModelAnimationResources::request(std::shared_ptr<const AssetCatalog> catalog,
                                                       AssetRef<SkeletonAsset> skeleton,
                                                       AssetRef<AnimationClipAsset> clip) {
    require(bool(catalog), "Model animation requires a selected catalog");
    const auto binding = select_animation_clip(*catalog, skeleton, clip);
    const auto model = binding.model;
    const auto& revision = binding.skeleton_revision;
    const auto generation = binding.skeleton_generation;
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
            std::map<std::size_t, AssetId> node_assets;
            for (const auto& value : selection.index.members)
                if (value.node)
                    require(node_assets
                                .emplace(*value.node, selection.bindings.at(value.identity.address))
                                .second,
                            "Duplicate model node identity in skeleton selection");
            // Older cooked bundles remain useful for pose inspection but cannot
            // bind scene nodes without durable node identities. Never bind by name.
            if (selection.index.version >= 3) {
                for (const auto node : data->joint_nodes) {
                    require(node_assets.contains(node),
                            "Skeleton joint has no durable model node identity");
                    data->joint_assets.push_back(node_assets.at(node));
                }
            }
            require(!stop.stop_requested(), "Skeleton load cancelled");
            const auto bytes = data->resident_bytes();
            return ResourceCandidate<SkeletonAsset>{std::move(data), {0, 0, 0, 0, bytes}};
        });
    auto clip_ticket = clips_.request(
        clip, binding.clip_revision, binding.clip_generation,
        [project, catalog, model, revision, generation, clip, skeleton,
         baked = binding.baked](std::stop_token stop) {
            const auto selection =
                asset_detail::load_model_selection(project, *catalog, model, stop);
            selected(selection, revision, generation);
            auto data = std::make_unique<ClipResourceData>();
            data->model = model;
            data->skeleton = skeleton.id;
            if (baked) {
                auto value = load_baked_animation_clip(project, catalog->records().at(clip.id),
                                                       selection, stop);
                data->native = std::move(value.native);
                data->morphs =
                    std::make_unique<const asset_detail::MorphAnimation>(nlohmann::json::array());
                data->has_transform_channels = true;
                for (const auto node : value.joint_nodes)
                    for (const auto path :
                         {AnimatedTransformPath::Translation, AnimatedTransformPath::Rotation,
                          AnimatedTransformPath::Scale})
                        data->transform_channels.push_back({node, path});
                const auto bytes = data->resident_bytes();
                return ResourceCandidate<AnimationClipAsset>{std::move(data), {0, 0, 0, 0, bytes}};
            }
            const auto& clip_member = selection.member(clip.id);
            require(selection.bindings.at(clip_member.bindings.at("skeleton")) == skeleton.id,
                    "Cooked animation skeleton binding differs");
            data->native = std::make_shared<Clip>(selection.bytes(clip_member));
            const auto& plan = selection.index.hierarchy.at("animation").at("plan");
            for (const auto& entry : plan.at("clips"))
                if (entry.at("file") == clip_member.artifact.file) {
                    data->morphs = std::make_unique<const asset_detail::MorphAnimation>(
                        entry.at("morph_tracks"));
                    for (const auto& track : entry.at("morph_tracks")) {
                        const auto node = track.at("node").get<std::size_t>();
                        auto defaults = selection.index.hierarchy.at("nodes")
                                            .at(node)
                                            .at("weights")
                                            .get<std::vector<float>>();
                        require(defaults.size() == track.at("components").get<std::size_t>(),
                                "Morph curve defaults differ from admitted target count");
                        data->morph_defaults.push_back({node, std::move(defaults)});
                    }
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
    return {skeleton, clip,       std::move(skeleton_ticket), std::move(clip_ticket), model,
            revision, generation, binding.clip_revision,      binding.clip_generation};
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
    require(s.identity().revision == r.revision && c.identity().revision == r.clip_revision &&
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
