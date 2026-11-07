#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace forge {
inline constexpr unsigned audio_routing_profile_version = 1;
inline constexpr unsigned audio_bus_effects_profile_version = 1;
inline constexpr std::size_t audio_custom_bus_limit = 16;
inline constexpr std::size_t audio_bus_send_limit = 3;
inline constexpr std::size_t audio_bus_key_limit = 64;
inline constexpr std::size_t audio_bus_label_limit = 128;
inline constexpr double audio_routing_gain_limit = 16;
using AudioBusVolumes = std::map<std::string, float, std::less<>>;
bool valid_audio_bus_key(std::string_view) noexcept;
struct AudioBusSend {
    std::string target;
    float gain = 1;
    bool operator==(const AudioBusSend&) const = default;
};
// Linear processors, in high-pass -> low-pass -> echo -> reverb order.
// Disabled parameters are retained; defaults serialize without an effects block.
struct AudioBusEffects {
    bool low_pass_enabled = false, high_pass_enabled = false;
    float low_pass_hz = 20000, high_pass_hz = 20;
    bool echo_enabled = false;
    float echo_seconds = .25f, echo_feedback = .35f;
    bool reverb_enabled = false;
    float reverb_decay = 1.5f, reverb_damping = .5f, reverb_wet = .25f;
    bool active() const {
        return low_pass_enabled || high_pass_enabled || (echo_enabled && echo_feedback > 0) ||
               (reverb_enabled && reverb_wet > 0);
    }
    bool operator==(const AudioBusEffects&) const = default;
};
struct AudioBusDefinition {
    std::string key, label;
    float volume = 1;
    std::string output = "master";
    std::vector<AudioBusSend> sends;
    AudioBusEffects effects{};
    bool operator==(const AudioBusDefinition&) const = default;
};
struct AudioRouting {
    std::vector<AudioBusDefinition> buses;
    bool operator==(const AudioRouting&) const = default;
};
struct AudioBusWeights {
    std::string key;
    std::array<double, 4> terminals{};
    bool operator==(const AudioBusWeights&) const = default;
};
// Derived immutable scalar path coefficients; bus DSP is not projected here.
// Settings own topology; active effects use the runtime's native bus graph.
class AudioRoutingProjection {
    std::vector<AudioBusWeights> buses_;
    AudioBusVolumes volumes_;

  public:
    static AudioRoutingProjection compile(const AudioRouting&, const AudioBusVolumes& = {},
                                          bool require_complete_volumes = false);
    const std::vector<AudioBusWeights>& buses() const { return buses_; }
    const AudioBusVolumes& volumes() const { return volumes_; }
    float gain(std::string_view custom_bus, std::uint32_t stock_bus,
               const std::array<float, 3>& channels) const;
};
void validate_audio_routing(const AudioRouting&);
void validate_audio_bus_effects(const AudioBusEffects&);
AudioBusEffects audio_bus_effects_from_json(const nlohmann::json&);
nlohmann::json audio_bus_effects_json(const AudioBusEffects&);
void validate_audio_bus_volumes(const AudioBusVolumes&);
AudioRouting audio_routing_from_json(const nlohmann::json&);
nlohmann::json audio_routing_json(const AudioRouting&);
AudioBusVolumes audio_bus_volumes_from_json(const nlohmann::json&);
} // namespace forge
