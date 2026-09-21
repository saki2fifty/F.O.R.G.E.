#pragma once
#include <forge/render_scene.hpp>
namespace forge {
struct AnimationDebugSkeleton {
    EntityId owner;
    std::vector<int> parents;
    std::vector<std::optional<Double3>> positions;
};
// Prepared once per scene snapshot, then projected for each displayed camera.
// Model bones use actual resolved ECS joint worlds; legacy standalone Animator
// inspection uses its owner's world multiplied by the Ozz model pose.
std::vector<AnimationDebugSkeleton> prepare_animation_debug(const nlohmann::json&,
                                                            const RenderScene*);
} // namespace forge
