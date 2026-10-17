#pragma once
#include <cmath>
#include <flecs.h>
#include <forge/animation_components.hpp>
// Call from an owner-thread C++ system before Animation. The sequence's existing
// clock drives its object clips and camera shots; no camera enabled flag is edited.
inline bool forge_set_sequence_speed(flecs::entity controller, float speed) {
    if (!controller || !controller.has<forge::AnimationSequence>() || !std::isfinite(speed) ||
        speed < 0 || speed > 4)
        return false;
    auto sequence = controller.get<forge::AnimationSequence>();
    sequence.playback_speed = speed;
    controller.set(sequence);
    return true;
}
