#pragma once
#include "animation_asset.hpp"
#include "model_selection.hpp"
#include <forge/animation_components.hpp>
#include <set>

namespace forge::animation_detail {
inline constexpr auto retarget_importer = "forge.animation.retarget";
inline constexpr auto retarget_recipe = "forge.animation.retarget.v1";
inline constexpr auto baked_clip_format = "forge.skeletal-clip";
inline constexpr unsigned baked_clip_profile_version = 1;
inline constexpr std::size_t baked_clip_descriptor_limit = 32 * 1024;
struct AnimationClipSelection {
    AssetId model;
    std::string skeleton_revision, clip_revision;
    std::uint64_t skeleton_generation = 0, clip_generation = 0;
    bool baked = false;
};
// Shared binding admission for native playback and authoring consumers. Imported
// pairs retain one-family checks; baked clips require an exact target dependency.
AnimationClipSelection select_animation_clip(const AssetCatalog&, AssetRef<SkeletonAsset>,
                                             AssetRef<AnimationClipAsset>);
std::set<AssetId> compatible_animation_clips(const AssetCatalog&, AssetRef<SkeletonAsset>);
struct AnimationSkeletonInspection {
    AssetId skeleton, model;
    std::string revision;
    std::uint64_t generation = 0;
    std::vector<AssetId> joints;
    std::vector<std::int16_t> parents;
    std::vector<LocalPose> rest;
    std::vector<std::string> labels;
};
// Bounded copied metadata for native/MCP mapping controls, without source parsing
// or a live pose world. Native views invoke this on their preparation worker.
AnimationSkeletonInspection inspect_animation_skeleton(const std::filesystem::path&,
                                                       const AssetCatalog&, AssetRef<SkeletonAsset>,
                                                       std::stop_token = {});
struct BakedAnimationClip {
    AssetId asset, skeleton, model;
    std::string skeleton_revision;
    std::vector<std::size_t> joint_nodes;
    std::shared_ptr<const Clip> native;
};
BakedAnimationClip decode_baked_animation_clip(std::span<const ArtifactFile>);
BakedAnimationClip admit_baked_animation_clip(const CachedArtifact&, const AssetRecord&);
BakedAnimationClip load_baked_animation_clip(const std::filesystem::path&, const AssetRecord&,
                                             const asset_detail::ModelSelection&,
                                             std::stop_token = {});
} // namespace forge::animation_detail
