#pragma once
#include <algorithm>
#include <cmath>
namespace forge {
// Ephemeral ruler coordinates in source microseconds; no document ownership.
struct AnimationTimelineView {
    double duration = 1, first = 0, span = 1;
    void reconcile(double value) {
        if (!std::isfinite(value) || value < 1 || value > 3600000000.)
            return;
        const bool full = first == 0 && span == duration;
        duration = value;
        if (full)
            fit_clip();
        else
            clamp();
    }
    void clamp() {
        span = std::clamp(std::isfinite(span) ? span : duration, 1., duration);
        first = std::clamp(std::isfinite(first) ? first : 0., 0., duration - span);
    }
    void fit_clip() {
        first = 0;
        span = duration;
    }
    void fit(double low, double high) {
        if (!std::isfinite(low) || !std::isfinite(high) || low < 0 || high < low || high > duration)
            return;
        const double padding = high == low ? std::max(1., duration * .025) : (high - low) * .1;
        first = low - padding;
        span = high - low + 2 * padding;
        clamp();
    }
    void zoom(double factor, double anchor) {
        if (!std::isfinite(factor) || factor <= 0 || !std::isfinite(anchor))
            return;
        anchor = std::clamp(anchor, 0., 1.);
        const auto time = first + anchor * span;
        span = std::clamp(span / factor, 1., duration);
        first = time - anchor * span;
        clamp();
    }
    void pan(double offset) {
        if (std::isfinite(offset)) {
            first += offset;
            clamp();
        }
    }
    double time(double fraction) const { return first + std::clamp(fraction, 0., 1.) * span; }
    double fraction(double time) const { return (time - first) / span; }
    bool visible(double time) const { return time >= first && time <= first + span; }
};
} // namespace forge
