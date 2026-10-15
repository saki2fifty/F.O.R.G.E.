#pragma once
#include <cmath>
#include <flecs.h>
#include <forge/animation_components.hpp>
#include <forge/render_components.hpp>
// Material tracks use the ordinary TransformAnimator. Change its existing
// playback speed on a standalone object; the animation owner samples numeric
// values into MeshRenderer without changing the shared material asset.
inline bool forge_set_material_animation_speed(flecs::entity object, float speed) {
    if (!object || !object.has<forge::MeshRenderer>() || !object.has<forge::TransformAnimator>() ||
        !std::isfinite(speed) || speed < 0 || speed > 4)
        return false;
    auto animator = object.get<forge::TransformAnimator>();
    if (animator.sequence.entity)
        return false; // A sequence controller owns the speed of its bound tracks.
    animator.playback_speed = speed;
    object.set(animator);
    return true;
}
