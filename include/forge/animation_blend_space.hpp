#pragma once
#include <forge/animation_components.hpp>
#include <nlohmann/json.hpp>
#include <span>
#include <vector>
namespace forge {
inline constexpr std::size_t animation_blend_space_byte_limit = 256 * 1024;
inline constexpr std::size_t animation_blend_space_cooked_byte_limit =
    animation_blend_space_byte_limit + 128;
struct AnimationBlendSample {
    AssetId id;
    AssetRef<AnimationClipAsset> clip;
    double position = 0;
};
struct AnimationBlendPair {
    std::size_t first = 0, second = 0;
    double weight = 0;
};
struct AnimationBlendSpace {
    AssetId asset, skeleton;
    double cycle_seconds = 1;
    std::vector<AnimationBlendSample> samples;
    // Samples are admitted and sorted once; binding chooses at most two neighbors.
    AnimationBlendPair select(double position) const;
    std::size_t resident_bytes() const;
};
struct AnimationBlendSpaceSource {
    nlohmann::json document;
    static AnimationBlendSpaceSource create(AssetId);
    static AnimationBlendSpaceSource parse(std::span<const std::byte>);
    static AnimationBlendSpaceSource parse_cooked(std::span<const std::byte>);
    AssetId asset() const;
    void validate() const;
    AnimationBlendSpace compiled() const;
    std::vector<std::byte> cook() const;
};
AnimationBlendSpace decode_animation_blend_space(std::span<const std::byte>, AssetId);
} // namespace forge
