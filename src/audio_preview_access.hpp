#pragma once
#include "audio_asset.hpp"
#include <forge/audio.hpp>
namespace forge {
// Private editor consumer. Never reads a mutable data-source cursor from the UI.
struct AudioPreviewAccess {
    static const asset_detail::AudioClipData& clip(AudioRuntime&, EntityRef);
    static double elapsed(AudioRuntime&, EntityRef);
    static double seek(AudioRuntime&, EntityRef, double seconds);
};
} // namespace forge
