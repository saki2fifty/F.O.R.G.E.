#pragma once
#include <cstddef>
#include <cstdint>
#include <forge/asset_ref.hpp>
#include <string>
#include <vector>
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
    // FORGE distance curves: 0 inverse, 1 linear, 2 exponential.
    std::uint32_t attenuation = 0;
    float rolloff = 1;
    bool cone_enabled = false;
    // Full cone openings in degrees; source forward is effective local negative Z.
    float inner_angle = 60, outer_angle = 120, outer_gain = .25f;
    // 0 Master, 1 Music, 2 Effects, 3 Dialogue; stable FORGE channel values.
    std::uint32_t bus = 0;
    // Zero disables motion pitch; one uses ordinary relative velocity in meters/s.
    float doppler = 0;
    // Keep music/UI playback advancing when only gameplay is paused.
    bool pause_with_game = true;
    bool low_pass_enabled = false, high_pass_enabled = false;
    float low_pass_hz = 20000, high_pass_hz = 20;
    bool echo_enabled = false;
    float echo_seconds = .25f, echo_feedback = .35f;
    bool reverb_enabled = false;
    float reverb_decay = 1.5f, reverb_damping = .5f, reverb_wet = .25f;
    // A project-defined key overrides the retained stock bus; empty uses bus0..3.
    std::string custom_bus{};
    bool operator==(const AudioSource&) const = default;
};
struct AudioListener {
    bool enabled = true;
    bool operator==(const AudioListener&) const = default;
};
inline constexpr unsigned sound_cue_profile_version = 1;
inline constexpr unsigned sound_cue_timing_profile_version = 1;
inline constexpr double sound_cue_delay_limit_seconds = 10;
inline constexpr unsigned sound_cue_repeat_profile_version = 1;
inline constexpr double sound_cue_repeat_min_seconds = .25;
inline constexpr double sound_cue_repeat_max_seconds = 3600;
inline constexpr std::size_t sound_cue_repeat_instance_limit = 32;
inline constexpr std::size_t sound_cue_node_limit = 32;
inline constexpr std::size_t sound_cue_child_limit = 8;
inline constexpr std::size_t sound_cue_output_limit = 16;
struct SoundCueEdge {
    std::uint32_t target = 0, weight = 1;
    bool operator==(const SoundCueEdge&) const = default;
};
struct SoundCueNode {
    // 0 Source, 1 Layer, 2 Weighted choice, 3 Ordered choice (one child per trigger).
    std::uint32_t kind = 0;
    EntityRef source{};
    std::vector<SoundCueEdge> children;
    // Source-only offset on the native output clock, independent of source pitch.
    double delay_seconds = 0;
    bool operator==(const SoundCueNode&) const = default;
};
// Event composition only: AudioSource retains all clip, spatial and voice ownership.
struct SoundCue {
    bool enabled = true;
    std::uint32_t seed = 1, root = 0;
    std::vector<SoundCueNode> nodes;
    double repeat_interval_seconds = 0;
    bool repeat_on_start = false, repeat_pause_with_game = true;
    bool operator==(const SoundCue&) const = default;
};
} // namespace forge
