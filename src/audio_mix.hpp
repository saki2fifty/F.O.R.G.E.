#pragma once
#include <array>
#include <cmath>
#include <forge/audio_routing.hpp>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
namespace forge {
// Private Play control projection. AudioRuntime remains the mixer owner.
struct AudioMixValues {
    float master = 1;
    std::array<float, 3> channels{1, 1, 1};
    // Missing retains custom gains; an explicit map must match the runtime graph.
    std::optional<AudioBusVolumes> buses{};
    bool operator==(const AudioMixValues&) const = default;
    nlohmann::json document() const {
        nlohmann::json result{{"master_volume", master}, {"channel_volumes", channels}};
        if (buses)
            result["bus_volumes"] = *buses;
        return result;
    }
    static AudioMixValues parse(const nlohmann::json& value) {
        if (!value.is_object() ||
            (value.size() != 2 && !(value.size() == 3 && value.contains("bus_volumes"))))
            throw std::runtime_error(
                "Audio mix requires master_volume, channel_volumes and optional bus_volumes");
        return status(value);
    }
    static AudioMixValues status(const nlohmann::json& value) {
        const auto& channels = value.at("channel_volumes");
        if (!channels.is_array() || channels.size() != 3)
            throw std::runtime_error("Audio mix requires exactly three channel volumes");
        AudioMixValues result;
        result.master = volume(value.at("master_volume"));
        for (unsigned i = 0; i < 3; ++i)
            result.channels[i] = volume(channels[i]);
        if (value.contains("bus_volumes"))
            result.buses = audio_bus_volumes_from_json(value.at("bus_volumes"));
        return result;
    }

  private:
    static float volume(const nlohmann::json& value) {
        if (!value.is_number())
            throw std::runtime_error("Audio mix volumes must be numbers");
        const double number = value.get<double>();
        if (!std::isfinite(number) || number < 0 || number > 1)
            throw std::runtime_error("Audio mix volumes must be finite and between zero and one");
        return float(number);
    }
};
} // namespace forge
