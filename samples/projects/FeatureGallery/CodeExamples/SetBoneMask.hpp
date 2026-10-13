#pragma once
#include <flecs.h>
#include <forge/animation_components.hpp>
// Call from ordinary C++ at a fixed tick. Runtime admission resolves the typed
// asset once and keeps the last working pose if it is missing or incompatible.
inline bool forge_set_bone_mask(flecs::entity actor,
                                forge::AssetRef<forge::AnimationMaskAsset> mask) {
    if (!actor || !actor.has<forge::Animator>())
        return false;
    auto config = actor.get<forge::Animator>();
    if (config.states.id || (mask.id && !config.blend_clip.id))
        return false;
    config.blend_mask = mask;
    actor.set(config);
    return true;
}
