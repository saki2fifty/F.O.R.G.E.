#pragma once
#include <forge/animation_components.hpp>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>
namespace forge {
inline constexpr std::size_t animation_states_byte_limit = 256 * 1024;
struct AnimationState {
    std::uint32_t id = 0;
    std::string name;
    AssetRef<AnimationClipAsset> clip;
    bool loop = true;
    float speed = 1, transition_seconds = .25f;
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
    AssetId asset() const;
    void validate() const;
    AnimationStates compiled() const;
    std::vector<std::byte> cook() const;
};
AnimationStates decode_animation_states(std::span<const std::byte>, AssetId);
} // namespace forge
