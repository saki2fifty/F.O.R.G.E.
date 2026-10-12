#pragma once
#include <array>
#include <cmath>
#include <nlohmann/json.hpp>
#include <stdexcept>
namespace forge {
// Private Play control projection. AudioRuntime remains the mixer owner.
struct AudioMixValues {
    float master = 1;
    std::array<float, 3> channels{1, 1, 1};
    bool operator==(const AudioMixValues&) const = default;
    nlohmann::json document() const {
        return {{"master_volume", master}, {"channel_volumes", channels}};
    }
    static AudioMixValues parse(const nlohmann::json& value) {
        if (!value.is_object() || value.size() != 2)
            throw std::runtime_error("Audio mix requires master_volume and channel_volumes");
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
