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
struct AudioBusDefinition {
    std::string key, label;
    float volume = 1;
    std::string output = "master";
    std::vector<AudioBusSend> sends;
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
// Derived immutable scalar data only. Settings remain the graph's owner.
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
void validate_audio_bus_volumes(const AudioBusVolumes&);
AudioRouting audio_routing_from_json(const nlohmann::json&);
nlohmann::json audio_routing_json(const AudioRouting&);
AudioBusVolumes audio_bus_volumes_from_json(const nlohmann::json&);
} // namespace forge
