#pragma once
#include <flecs.h>
#include <forge/animation_components.hpp>
// Call from an ordinary C++ Gameplay System after assigning compatible clips.
// The durable joint comes from this imported model. Empty selects the whole rig.
inline void forge_set_blend_subtree(flecs::entity actor,
                                    forge::AssetRef<forge::ModelNodeAsset> joint) {
    auto animator = actor.get<forge::Animator>();
    animator.blend_root = joint;
    actor.set(animator);
}
