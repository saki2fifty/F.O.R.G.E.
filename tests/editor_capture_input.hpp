#pragma once
#include <cfloat>
#include <cmath>
#include <imgui.h>
namespace forge::test {
// Static capture input must be applied after the SDL platform update, which can
// replace queued positions. Authoring state is changed only by normal widget input.
struct EditorCaptureInput {
    float x = 0, y = 0;
    bool valid = false;
    bool active(unsigned stage, unsigned frames) const {
        return valid && (((stage == 121 || stage == 124) && frames >= 4 && frames <= 25) ||
                         (stage == 142 && frames >= 4 && frames <= 22));
    }
    static bool document_focus_allowed(unsigned stage, bool popup_open) {
        // A forced parent-window focus steals keyboard navigation from its combo.
        return !popup_open || (stage != 121 && stage != 124);
    }
    void queue(ImGuiIO& io, ImVec2 at, int event) {
        valid = std::isfinite(at.x) && std::isfinite(at.y);
        x = valid ? at.x : -FLT_MAX;
        y = valid ? at.y : -FLT_MAX;
        io.AddMousePosEvent(x, y);
        if (event)
            io.AddMouseButtonEvent(0, event == 1);
    }
    void after_platform_update(ImGuiIO& io, unsigned stage, unsigned frames) const {
        io.ConfigInputTrickleEventQueue = false;
        const bool scripted = active(stage, frames);
        io.AddMousePosEvent(scripted ? x : -FLT_MAX, scripted ? y : -FLT_MAX);
    }
};
} // namespace forge::test
