#pragma once
#include <forge/assets.hpp>
namespace forge {
struct AudioClipAsset {
    static constexpr const char* type = "audio_clip";
};
struct AudioSource {
    AssetRef<AudioClipAsset> clip{};
    bool play_on_start = false, loop = false;
    float gain = 1, pitch = 1;
    bool spatialized = true;
    float minimum_distance = 1, maximum_distance = 100;
    bool operator==(const AudioSource&) const = default;
};
struct AudioListener {
    bool enabled = true;
    bool operator==(const AudioListener&) const = default;
};
} // namespace forge
