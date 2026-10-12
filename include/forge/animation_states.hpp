#pragma once
#include <forge/animation_components.hpp>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>
namespace forge {
inline constexpr std::size_t animation_states_byte_limit = 256 * 1024;
inline constexpr std::size_t animation_states_cooked_byte_limit = animation_states_byte_limit + 128;
struct AnimationMarker {
    std::uint32_t id = 0;
    std::string name;
    double phase = 0, value = 0;
};
struct AnimationState {
    std::uint32_t id = 0;
    std::string name;
    AssetRef<AnimationClipAsset> clip;
    bool loop = true;
    float speed = 1, transition_seconds = .25f;
    std::vector<AnimationMarker> markers;
    std::uint32_t exit_state = 0; // Optional automatic target; 0 keeps manual selection.
    float exit_phase = 1;         // Normalized gate, evaluated at a fixed-tick boundary.
};
struct AnimationStates {
    AssetId asset, skeleton;
    std::uint32_t entry = 0;
    std::vector<AnimationState> states;
    const AnimationState& state(std::uint32_t id) const;
    const AnimationState& state(std::string_view name) const;
    std::size_t resident_bytes() const;
};
struct AnimationStatesSource {
    nlohmann::json document;
    static AnimationStatesSource create(AssetId);
    static AnimationStatesSource parse(std::span<const std::byte>);
    static AnimationStatesSource parse_cooked(std::span<const std::byte>, std::uint32_t& version);
    AssetId asset() const;
    void validate() const;
    AnimationStates compiled() const;
    std::vector<std::byte> cook() const;
};
AnimationStates decode_animation_states(std::span<const std::byte>, AssetId);
} // namespace forge
