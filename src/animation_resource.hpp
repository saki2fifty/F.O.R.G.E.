#pragma once
#include "animation_asset.hpp"
#include "morph_animation.hpp"
#include <forge/animation_components.hpp>
#include <forge/assets.hpp>
#include <forge/resource.hpp>
namespace forge::animation_detail {
struct SkeletonResourceData {
    std::shared_ptr<const Skeleton> native;
    AssetId model;
    std::vector<std::size_t> joint_nodes;
    // Same order as joint_nodes/native joints, scoped by this immutable model revision.
    std::vector<AssetId> joint_assets;
    std::size_t resident_bytes() const;
};
enum class AnimatedTransformPath { Translation, Rotation, Scale };
struct AnimatedTransformChannel {
    std::size_t node = 0;
    AnimatedTransformPath path = AnimatedTransformPath::Translation;
};
struct ClipResourceData {
    std::shared_ptr<const Clip> native;
    std::unique_ptr<const asset_detail::MorphAnimation> morphs;
    AssetId model, skeleton;
    // Legacy companions have no channel intent. Never infer it from sampled values.
    bool has_transform_channels = false;
    std::vector<AnimatedTransformChannel> transform_channels;
    std::size_t resident_bytes() const;
};
} // namespace forge::animation_detail
namespace forge {
template <> struct ResourceTraits<SkeletonAsset> {
    using Data = animation_detail::SkeletonResourceData;
};
template <> struct ResourceTraits<AnimationClipAsset> {
    using Data = animation_detail::ClipResourceData;
};
} // namespace forge
namespace forge::animation_detail {
struct ModelAnimationRequest {
    AssetRef<SkeletonAsset> skeleton;
    AssetRef<AnimationClipAsset> clip;
    ResourceTicket skeleton_ticket, clip_ticket;
    AssetId model;
    std::string revision;
    std::uint64_t generation = 0;
};
struct ModelAnimationLease {
    ResourceLease<SkeletonAsset> skeleton;
    ResourceLease<AnimationClipAsset> clip;
    explicit operator bool() const { return bool(skeleton) && bool(clip); }
};
// Subsystem-owned CPU provider. No source conversion, live world mutation or
// device access. Owner destruction must follow destruction of its samplers.
class ModelAnimationResources {
  public:
    explicit ModelAnimationResources(std::filesystem::path project);
    ModelAnimationRequest request(std::shared_ptr<const AssetCatalog> catalog,
                                  AssetRef<SkeletonAsset> skeleton,
                                  AssetRef<AnimationClipAsset> clip);
    void pump();
    ModelAnimationLease acquire(const ModelAnimationRequest& request);
    // Recovery reconstruction is quiescent and unpublished. Never call from a tick.
    bool prepare_recovery(const ModelAnimationRequest&, std::chrono::milliseconds timeout);
    void unload(const ModelAnimationRequest&);
    void unload_skeleton(AssetRef<SkeletonAsset>);
    void unload_clip(AssetRef<AnimationClipAsset>);
    void close();
    ResourceStatistics skeleton_statistics() const;
    ResourceStatistics clip_statistics() const;

  private:
    std::filesystem::path project_;
    ResourcePool<SkeletonAsset> skeletons_;
    ResourcePool<AnimationClipAsset> clips_;
};
} // namespace forge::animation_detail
