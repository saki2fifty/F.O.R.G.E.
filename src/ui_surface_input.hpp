#pragma once
#include "frame_renderer.hpp"
#include <forge/ui_presenter.hpp>

namespace forge {
// Host composition only: input and geometry remain with their existing owners.
// Resolve the active frame at call time, including staged scene replacement.
inline UiSurfaceCallbacks ui_surface_callbacks(std::function<const FrameRenderer*()> active) {
    return {[active](int x, int y, std::optional<UiSurfaceHit> retained) {
                const auto* frame = active();
                return frame ? frame->pick_ui_surface(x, y, retained) : std::nullopt;
            },
            [active](const UiSurfaceHit& hit) {
                const auto* frame = active();
                return frame ? frame->project_ui_surface(hit) : std::nullopt;
            }};
}
} // namespace forge
