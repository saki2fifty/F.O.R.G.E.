#pragma once
#include <forge/audio.hpp>
namespace forge {
// Private editor Play access; no backend object or new gameplay service crosses the wire.
struct AudioSourceAccess {
    static Json status(AudioRuntime&, EntityRef);
    static void control(AudioRuntime&, EntityRef, bool play);
};
} // namespace forge
