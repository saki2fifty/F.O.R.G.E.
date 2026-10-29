#pragma once
#include "temporal_frame_sample.hpp"
#include <cmath>
#include <optional>
#include <stdexcept>

namespace forge {
enum class MotionSnapshotAction { Live, Refresh, Hold };
struct TemporalSampleTransition {
    bool reset_geometry = false;
    bool reset_accumulation = false;
    MotionSnapshotAction motion = MotionSnapshotAction::Live;
    bool operator==(const TemporalSampleTransition&) const = default;
};
// Pure policy for borrowed capture keys. Camera/resource/failure invalidation
// remains with the view/frame owner; this neither stores nor advances time.
inline TemporalSampleTransition
temporal_sample_transition(const std::optional<TemporalFrameSample>& previous,
                           const std::optional<TemporalFrameSample>& current) {
    for (const auto* sample : {&previous, &current})
        if (*sample &&
            (!(*sample)->operation || !std::isfinite((*sample)->seconds) || (*sample)->seconds < 0))
            throw std::invalid_argument(
                "Temporal sample requires an operation and finite nonnegative time");
    if (!current)
        return {bool(previous), bool(previous), MotionSnapshotAction::Live};
    if (!previous || previous->operation != current->operation ||
        current->seconds < previous->seconds)
        return {true, true, MotionSnapshotAction::Refresh};
    if (current->seconds == previous->seconds)
        return {false, false, MotionSnapshotAction::Hold};
    // A new explicit image needs fresh AA accumulation. The real preceding
    // completed pose still supplies motion for its first render, before warmup.
    return {false, true, MotionSnapshotAction::Refresh};
}
} // namespace forge
