#pragma once
#include "temporal_sample_policy.hpp"
#include <limits>

inline void test_temporal_sample_policy() {
    using namespace forge;
    const auto check = [](bool value, const char* why) {
        if (!value)
            throw std::runtime_error(why);
    };
    const auto first = AssetId::parse("30e99eba-b886-4dd6-b608-7b76d41bccb4");
    const auto second = AssetId::parse("c01c0529-37a5-4dfe-b574-801d793c6e68");
    const std::optional<TemporalFrameSample> at_start{TemporalFrameSample{first, 1}};
    check(temporal_sample_transition({}, {}) == TemporalSampleTransition{},
          "Ordinary presentation unexpectedly owns an explicit sample snapshot");
    check(temporal_sample_transition({}, at_start) ==
              TemporalSampleTransition{true, true, MotionSnapshotAction::Refresh},
          "First capture sample reused unrelated previous geometry or color");
    auto previous = at_start;
    for (double time : {1.25, 1.5, 2., 1000.}) {
        const std::optional<TemporalFrameSample> current{TemporalFrameSample{first, time}};
        check(temporal_sample_transition(previous, current) ==
                  TemporalSampleTransition{false, true, MotionSnapshotAction::Refresh},
              "Forward capture discarded real preceding geometry or retained AA color");
        for (unsigned warmup = 0; warmup < 8; ++warmup)
            check(temporal_sample_transition(current, current) ==
                      TemporalSampleTransition{false, false, MotionSnapshotAction::Hold},
                  "Frozen sample warmup would overwrite its first real motion field");
        previous = current;
    }
    check(temporal_sample_transition(previous, at_start) ==
              TemporalSampleTransition{true, true, MotionSnapshotAction::Refresh},
          "Backward explicit seek retained unrelated correspondence");
    for (double time : {0., 1., 1001.})
        check(temporal_sample_transition(at_start, TemporalFrameSample{second, time}) ==
                  TemporalSampleTransition{true, true, MotionSnapshotAction::Refresh},
              "New export operation retained another lease's history");
    check(temporal_sample_transition(previous, {}) ==
              TemporalSampleTransition{true, true, MotionSnapshotAction::Live},
          "Leaving capture retained its frozen motion snapshot");
    const double adjacent = std::nextafter(1., 2.);
    check(temporal_sample_transition(at_start, TemporalFrameSample{first, adjacent}).motion ==
              MotionSnapshotAction::Refresh,
          "Distinct representable samples were merged by a timing tolerance");
    for (const auto& invalid :
         {TemporalFrameSample{{}, 0}, TemporalFrameSample{first, -1},
          TemporalFrameSample{first, std::numeric_limits<double>::infinity()},
          TemporalFrameSample{first, std::numeric_limits<double>::quiet_NaN()}})
        for (bool invalid_previous : {false, true}) {
            bool rejected = false;
            try {
                (void)temporal_sample_transition(
                    invalid_previous ? std::optional(invalid) : at_start,
                    invalid_previous ? at_start : std::optional(invalid));
            } catch (const std::invalid_argument&) {
                rejected = true;
            }
            check(rejected, "Invalid borrowed capture identity/time was admitted");
        }
}
