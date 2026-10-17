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
struct AnimationMaskAsset {
    static constexpr const char* type = "animation_mask";
};
struct AnimationBlendSpaceAsset {
    static constexpr const char* type = "animation_blend_space";
};
struct TransformAnimationAsset {
    static constexpr const char* type = "transform_animation";
};
// Scene/Prefab-owned coordination of existing clips; AnimationRuntime owns time.
struct AnimationSequence {
    double duration = 4;
    bool enabled = true, play_on_start = true, loop = false;
    float playback_speed = 1;
    bool operator==(const AnimationSequence&) const = default;
};
// Scene-owned shot arrangement. Selection is derived in copied presentation;
// authored Camera components and the sequence's existing clock remain unchanged.
struct CameraShot {
    EntityRef sequence{}, camera{};
    double start_seconds = 0, end_seconds = 1;
    bool enabled = true;
    bool operator==(const CameraShot&) const = default;
};
// A point event on the existing sequence clock. AudioSource owns the sound;
// forward playback requests one independent one-shot through AudioService.
struct AudioCue {
    EntityRef sequence{}, source{};
    double time_seconds = 0;
    bool enabled = true;
    bool operator==(const AudioCue&) const = default;
};
struct TransformAnimator {
    AssetRef<TransformAnimationAsset> clip{};
    bool enabled = true, play_on_start = true, loop = true;
    float playback_speed = 1;
    EntityRef sequence{};
    double sequence_start_seconds = 0;
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
    // 0 mixes absolute poses; 1 adds second-clip offsets relative to skeleton rest.
    std::uint32_t blend_mode = 0;
    // Optional reusable influence multiplied by the selected blend subtree.
    AssetRef<AnimationMaskAsset> blend_mask{};
    // One or two numeric axes select neighboring clips at one shared cycle phase.
    AssetRef<AnimationBlendSpaceAsset> blend_space{};
    float blend_position = 0;
    float blend_position_y = 0;
    bool operator==(const Animator&) const = default;
};
// Opt-in constraints on an imported model holder. Values are in skeleton model
// space; C++ systems can update them before the existing Animation phase.
struct TwoBoneIK {
    AssetRef<ModelNodeAsset> start{}, middle{}, end{};
    bool enabled = false;
    float weight = 1, soften = 1, twist_degrees = 0;
    float target_x = 1, target_y = 1, target_z = 0;
    float pole_x = 0, pole_y = 1, pole_z = 0;
    float axis_x = 0, axis_y = 0, axis_z = 1;
    bool operator==(const TwoBoneIK&) const = default;
};
struct AimIK {
    AssetRef<ModelNodeAsset> joint{};
    bool enabled = false;
    float weight = 1, twist_degrees = 0;
    float target_x = 1, target_y = 0, target_z = 1;
    float pole_x = 0, pole_y = 1, pole_z = 0;
    float forward_x = 1, forward_y = 0, forward_z = 0;
    float up_x = 0, up_y = 1, up_z = 0;
    bool operator==(const AimIK&) const = default;
};
} // namespace forge
