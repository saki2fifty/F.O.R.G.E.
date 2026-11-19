#pragma once
#include <forge/audio.hpp>
#include <optional>
#include <vector>

namespace forge {
// Private host access to copied mixer output; no device or voice changes.
struct AudioCaptureAccess {
    static void begin(AudioRuntime&);
    static void prepare(AudioRuntime&);
    // Trusted activation boundary: both Worlds' audio groups are suspended.
    // Device callbacks remain live; the existing buffer quiescence owns safety.
    static std::optional<std::uint64_t> handoff(AudioRuntime&, AudioRuntime&) noexcept;
    static void end(AudioRuntime&);
    static std::vector<float> read(AudioRuntime&, unsigned maximum_frames);
    static Json status(const AudioRuntime&);

  private:
    static void check_owner(const AudioRuntime&);
};
} // namespace forge
