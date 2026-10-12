#pragma once
#include <cmath>
#include <flecs.h>
#include <forge/animation_components.hpp>
// Call from an ordinary fixed C++ system before the Animation phase.
// Existing clocks are retained; duration controls the transition to the new mix.
inline bool forge_set_animation_mix(flecs::entity actor, std::uint32_t mode, float weight,
                                    float seconds = .25f) {
    if (!actor.has<forge::Animator>() || mode > 1 || !std::isfinite(weight) || weight < 0 ||
        weight > 1 || !std::isfinite(seconds) || seconds < 0 || seconds > 10)
        return false;
    auto value = actor.get<forge::Animator>();
    if (!value.blend_clip.id || value.states.id)
        return false;
    value.blend_mode = mode;
    value.blend_weight = weight;
    value.transition_seconds = seconds;
    actor.set(value);
    return true;
}
