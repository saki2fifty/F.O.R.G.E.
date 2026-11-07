#pragma once
#include "builtins.hpp"
#include <algorithm>
#include <forge/audio_routing.hpp>
#include <string_view>
namespace forge::detail {
// Shared AudioSource domain rules. Reflection remains the scalar authority.
inline void validate_audio_source(const AudioSource& value) {
    if (!value.custom_bus.empty() && !valid_audio_bus_key(value.custom_bus))
        throw std::runtime_error("Invalid custom audio bus key");
    if (value.maximum_distance < value.minimum_distance)
        throw std::runtime_error("Audio maximum distance must be at least minimum distance");
    if (value.outer_angle < value.inner_angle)
        throw std::runtime_error("Audio outer opening must be at least inner opening");
}
inline void validate_audio_source(flecs::entity entity, const AudioSource& value) {
    validate_reflected_value(entity, value);
    validate_audio_source(value);
}
inline AudioSource audio_source_value(const Json& value) {
    validate_components({{"forge.audio_source", value}});
    const auto& types = builtins();
    const auto codec = std::ranges::find_if(types, [](const Builtin& type) {
        return std::string_view(type.name) == "forge.audio_source";
    });
    if (codec == types.end())
        throw std::logic_error("AudioSource builtin is unavailable");
    const auto result = std::get<AudioSource>(codec->decode(value));
    validate_audio_source(result);
    return result;
}
} // namespace forge::detail
