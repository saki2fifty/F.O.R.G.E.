#pragma once
#include <forge/asset_ref.hpp>
#include <forge/model_asset.hpp>
namespace forge {
struct SkeletonAsset {
    static constexpr const char* type = "skeleton";
};
struct AnimationClipAsset {
    static constexpr const char* type = "animation_clip";
};
struct AnimationStatesAsset {
    static constexpr const char* type = "animation_states";
};
struct TransformAnimationAsset {
    static constexpr const char* type = "transform_animation";
};
struct TransformAnimator {
    AssetRef<TransformAnimationAsset> clip{};
    bool enabled = true, play_on_start = true, loop = true;
    float playback_speed = 1;
    bool operator==(const TransformAnimator&) const = default;
};
struct Animator {
    AssetRef<SkeletonAsset> skeleton{};
    AssetRef<AnimationClipAsset> clip{};
    bool enabled = true, play_on_start = true, loop = true;
    float playback_speed = 1;
    // Both clips share normalized phase. C++ may change blend_weight during Play.
    AssetRef<AnimationClipAsset> blend_clip{};
    float blend_weight = 0, transition_seconds = .25f;
    // Optional named clip set. Zero selects its entry state; IDs survive rename/reorder.
    AssetRef<AnimationStatesAsset> states{};
    std::uint32_t state = 0;
    // 0 preserves the existing pose; 1 moves the model root; 2 exposes a C++ delta.
    std::uint32_t root_motion = 0;
    // Null selects the unique skeleton root; explicit values use durable node IDs.
    AssetRef<ModelNodeAsset> root_joint{};
    // Null blends the whole skeleton; otherwise second clip affects this subtree.
    AssetRef<ModelNodeAsset> blend_root{};
    bool operator==(const Animator&) const = default;
};
} // namespace forge
