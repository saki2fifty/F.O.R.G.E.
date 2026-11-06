#pragma once
#include <forge/animation.hpp>
#include <string_view>
namespace forge {
// Private editor Play boundary; runtime clocks/pose transactions stay authoritative.
struct AnimationPlaybackAccess {
    static Json status(AnimationRuntime&, EntityRef);
    static Json tune_graph_input(AnimationRuntime&, EntityRef, const std::string& property,
                                 const Json& value);
    static Json tune_graph_inputs(AnimationRuntime&, EntityRef, const Json& fields);
    // Fresh copied World only; validate original placement before local isolation.
    static void prepare_sequence_preview_root(AnimationRuntime&, EntityRef);
    static void control(AnimationRuntime&, EntityRef, const Json& command);
    static Json tune_camera_shot(AnimationRuntime&, EntityRef, const Json& value);
    static bool rig_type(std::string_view type) {
        return type == "forge.two_bone_ik" || type == "forge.aim_ik";
    }
    static Json tune_rig(AnimationRuntime&, EntityRef, const std::string& type,
                         const std::string& property, const Json& value);
};
} // namespace forge
