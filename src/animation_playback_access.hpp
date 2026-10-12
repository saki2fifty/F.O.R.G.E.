#pragma once
#include <forge/animation.hpp>
namespace forge {
// Private editor Play boundary; runtime clocks/pose transactions stay authoritative.
struct AnimationPlaybackAccess {
    static Json status(AnimationRuntime&, EntityRef);
    static void control(AnimationRuntime&, EntityRef, const Json& command);
};
} // namespace forge
