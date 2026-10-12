#pragma once
#include <array>
#include <cmath>
#include <flecs.h>
#include <forge/animation_components.hpp>
// Update from the ordinary fixed C++ phase before Animation. Targets are in the
// skeleton's model space, independent of the holder's world translation/scale.
template <class Constraint>
bool forge_set_rig_target(flecs::entity actor, std::array<float, 3> target) {
    if (!actor.has<Constraint>())
        return false;
    for (auto coordinate : target)
        if (!std::isfinite(coordinate) || std::abs(coordinate) > 1000000)
            return false;
    auto value = actor.get<Constraint>();
    value.target_x = target[0];
    value.target_y = target[1];
    value.target_z = target[2];
    actor.set(value);
    return true;
}
