#pragma once
#include <forge/animation_components.hpp>
#include <nlohmann/json.hpp>
#include <span>
#include <vector>
namespace forge {
inline constexpr std::size_t animation_mask_byte_limit = 256 * 1024;
inline constexpr std::size_t animation_mask_cooked_byte_limit = animation_mask_byte_limit + 128;
struct AnimationMaskEntry {
    AssetId joint;
    float weight = 1;
    bool descendants = true;
};
struct AnimationMask {
    AssetId asset, skeleton;
    float default_weight = 0;
    std::vector<AnimationMaskEntry> entries;
    // Resolve durable joint references once at binding, never by name or array position.
    std::vector<float> weights(AssetId expected_skeleton, std::span<const AssetId> joints,
                               std::span<const std::int16_t> parents) const;
    std::size_t resident_bytes() const;
};
struct AnimationMaskSource {
    nlohmann::json document;
    static AnimationMaskSource create(AssetId);
    static AnimationMaskSource parse(std::span<const std::byte>);
    static AnimationMaskSource parse_cooked(std::span<const std::byte>);
    AssetId asset() const;
    void validate() const;
    AnimationMask compiled() const;
    std::vector<std::byte> cook() const;
};
AnimationMask decode_animation_mask(std::span<const std::byte>, AssetId);
} // namespace forge
