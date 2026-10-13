#pragma once
#include <cmath>
#include <flecs.h>
#include <forge/animation_components.hpp>
// Ordinary C++ selects a neighboring pair; the admitted cycle phase is preserved.
inline bool forge_set_blend_position(flecs::entity actor, float position) {
    if (!actor.has<forge::Animator>() || !std::isfinite(position) || std::abs(position) > 1000000)
        return false;
    auto config = actor.get<forge::Animator>();
    if (!config.blend_space.id || config.states.id || config.blend_clip.id ||
        config.blend_mask.id || config.blend_root.id || config.blend_mode != 0 ||
        config.blend_weight != 0)
        return false;
    config.blend_position = position;
    actor.set(config);
    return true;
}
