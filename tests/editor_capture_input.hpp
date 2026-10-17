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
    unsigned click_stage = 0, click_frame = 0;
    unsigned candidate_stage = 0, candidate_frame = 0, stable_frames = 0;
    ImVec2 candidate{};
    bool active(unsigned stage, unsigned frames) const {
        return valid && (((stage == 121 || stage == 124) && frames >= 4 && frames <= 25) ||
                         (stage == 142 && frames >= 4 && frames <= 22) ||
                         ((stage == 235 || stage == 238) && stage == click_stage &&
                          frames >= click_frame && frames <= click_frame + 4));
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
    void click_once(ImGuiIO& io, unsigned stage, unsigned frames, ImVec2 at, bool ready) {
        if (stage == click_stage)
            return;
        if (!ready || !std::isfinite(at.x) || !std::isfinite(at.y)) {
            stable_frames = 0;
            return;
        }
        // A first-use dock/window resize can move a ready widget before queued
        // input reaches NewFrame. Observe three consecutive stable positions.
        stable_frames = candidate_stage == stage && candidate_frame + 1 == frames &&
                                std::abs(candidate.x - at.x) <= .5f &&
                                std::abs(candidate.y - at.y) <= .5f
                            ? stable_frames + 1
                            : 1;
        candidate_stage = stage;
        candidate_frame = frames;
        candidate = at;
        if (stable_frames < 3)
            return;
        click_stage = stage;
        click_frame = frames;
        queue(io, at, 0); // Hover for a frame before pressing the actual widget.
    }
    void after_platform_update(ImGuiIO& io, unsigned stage, unsigned frames) {
        io.ConfigInputTrickleEventQueue = false;
        const bool scripted = active(stage, frames);
        io.AddMousePosEvent(scripted ? x : -FLT_MAX, scripted ? y : -FLT_MAX);
        // Deliver after SDL, even if the clicked menu/control disappears. Leaving
        // a stage must also release the button rather than leak it into a capture.
        if (pressed_ && (stage != click_stage || frames != click_frame + 1)) {
            io.AddMouseButtonEvent(0, false);
            pressed_ = false;
        }
        if (scripted && stage == click_stage && frames == click_frame + 1) {
            io.AddMouseButtonEvent(0, true);
            pressed_ = true;
        }
    }

  private:
    bool pressed_ = false;
};
} // namespace forge::test
