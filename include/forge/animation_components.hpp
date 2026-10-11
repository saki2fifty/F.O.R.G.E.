#pragma once
#include <forge/asset_ref.hpp>
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
    bool operator==(const Animator&) const = default;
};
} // namespace forge
