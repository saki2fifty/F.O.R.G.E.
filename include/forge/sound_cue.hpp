#pragma once
#include <array>
#include <forge/audio_components.hpp>
#include <nlohmann/json_fwd.hpp>

namespace forge {
struct SoundCueState {
    std::uint64_t triggers = 0;
    std::array<std::uint64_t, sound_cue_node_limit> visits{};
    bool operator==(const SoundCueState&) const = default;
};
struct SoundCueSelection {
    SoundCueState next;
    std::vector<EntityRef> sources;
    std::vector<double> delay_seconds;
};
// Immutable derived program; the Scene/Prefab component remains authoritative.
class SoundCueProgram {
    SoundCue cue_;
    std::array<std::size_t, sound_cue_node_limit> outputs_{};

  public:
    static SoundCueProgram compile(const SoundCue&);
    SoundCueSelection select(const SoundCueState&) const;
    const SoundCue& definition() const { return cue_; }
    std::size_t maximum_outputs() const;
};
SoundCue sound_cue_from_json(const nlohmann::json&);
nlohmann::json sound_cue_json(const SoundCue&);
} // namespace forge
