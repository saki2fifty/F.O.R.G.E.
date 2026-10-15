#pragma once
#include <cmath>
#include <flecs.h>
#include <forge/animation_components.hpp>
// Ordinary C++ selects neighboring clips; the admitted cycle phase is preserved.
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
// Set both axes atomically for a two-dimensional Blend Space.
inline bool forge_set_blend_position(flecs::entity actor, float x, float y) {
    if (!std::isfinite(y) || std::abs(y) > 1000000 || !actor.has<forge::Animator>())
        return false;
    const auto previous = actor.get<forge::Animator>();
    if (!std::isfinite(x) || std::abs(x) > 1000000 || !previous.blend_space.id ||
        previous.states.id || previous.blend_clip.id || previous.blend_mask.id ||
        previous.blend_root.id || previous.blend_mode != 0 || previous.blend_weight != 0)
        return false;
    auto config = previous;
    config.blend_position = x;
    config.blend_position_y = y;
    actor.set(config);
    return true;
}
