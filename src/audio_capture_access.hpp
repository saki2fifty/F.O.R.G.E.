#pragma once
#include <forge/audio.hpp>
#include <vector>

namespace forge {
// Private host access to copied mixer output; no device or voice changes.
struct AudioCaptureAccess {
    static void begin(AudioRuntime&);
    static void end(AudioRuntime&);
    static std::vector<float> read(AudioRuntime&, unsigned maximum_frames);
    static Json status(const AudioRuntime&);

  private:
    static void check_owner(const AudioRuntime&);
};
} // namespace forge
