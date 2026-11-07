#pragma once
#include <forge/asset_ref.hpp>
#include <forge/model_asset.hpp>
#include <vector>
namespace forge {
inline constexpr unsigned skeletal_sequence_runtime_profile_version = 1;
inline constexpr unsigned sequence_rate_runtime_profile_version = 1;
inline constexpr unsigned sequence_arrangement_runtime_profile_version = 1;
inline constexpr unsigned sequence_loop_runtime_profile_version = 1;
inline constexpr unsigned sequence_placement_runtime_profile_version = 1;
inline constexpr std::size_t sequence_clip_limit = 8;
inline constexpr std::size_t sequence_placement_limit = 8;
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
struct AnimationGraphAsset {
    static constexpr const char* type = "animation_graph";
};
struct TransformAnimationAsset {
    static constexpr const char* type = "transform_animation";
};
template <class ClipAsset> struct SequenceClipPlacement {
    AssetRef<ClipAsset> clip{};
    double start_seconds = 0;
    bool operator==(const SequenceClipPlacement&) const = default;
};
struct SequenceControllerPlacement {
    double start_seconds = 0, rate = 1;
    bool loop = false;
    bool operator==(const SequenceControllerPlacement&) const = default;
};
// Scene/Prefab-owned coordination of existing clips; AnimationRuntime owns time.
struct AnimationSequence {
    double duration = 4;
    bool enabled = true, play_on_start = true, loop = false;
    float playback_speed = 1;
    // Optional placement on a parent controller; bound timing comes from its root.
    EntityRef sequence{};
    double sequence_start_seconds = 0;
    // Local seconds per parent second; standalone playback_speed stays separate.
    double sequence_rate = 1;
    // Repeat this placement through the remaining parent span; ignored on roots.
    bool sequence_loop = false;
    std::vector<SequenceControllerPlacement> sequence_extra_placements{};
    bool operator==(const AnimationSequence&) const = default;
};
// Scene-owned shot arrangement. Selection is derived in copied presentation;
// authored Camera components and the sequence's existing clock remain unchanged.
struct CameraShot {
    EntityRef sequence{}, camera{};
    double start_seconds = 0, end_seconds = 1;
    bool enabled = true;
    double blend_in_seconds = 0; // Adjacent incoming view/lens transition; zero retains a cut.
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
    // Optional second object-transform clip on the same sequence clock. Its
    // natural overlap with the primary clip defines a linear crossfade.
    AssetRef<TransformAnimationAsset> sequence_next_clip{};
    double sequence_next_start_seconds = 0;
    std::vector<SequenceClipPlacement<TransformAnimationAsset>> sequence_extra_clips{};
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
    // Graph mode owns clip selection. Each unchecked override uses the named
    // slot's source default rather than silently replacing it with zero.
    AssetRef<AnimationGraphAsset> graph{};
    bool graph_override_1 = false, graph_override_2 = false, graph_override_3 = false,
         graph_override_4 = false;
    float graph_input_1 = 0, graph_input_2 = 0, graph_input_3 = 0, graph_input_4 = 0;
    // Direct clips may follow an existing Scene sequence. These fields do not
    // change the shared-phase meaning of standalone blend_clip.
    EntityRef sequence{};
    double sequence_start_seconds = 0;
    AssetRef<AnimationClipAsset> sequence_next_clip{};
    double sequence_next_start_seconds = 0;
    std::vector<SequenceClipPlacement<AnimationClipAsset>> sequence_extra_clips{};
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
