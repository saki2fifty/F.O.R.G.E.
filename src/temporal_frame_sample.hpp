#pragma once
#include <forge/identity.hpp>

namespace forge {
// Borrowed explicit sample identity, supplied by the existing capture owner.
// It resets presentation history without owning or advancing simulation time.
struct TemporalFrameSample {
    AssetId operation;
    double seconds = 0;
    bool operator==(const TemporalFrameSample&) const = default;
};
} // namespace forge
