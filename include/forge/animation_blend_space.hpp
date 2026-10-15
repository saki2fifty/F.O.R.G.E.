#pragma once
#include <array>
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
    double position_y = 0;
};
struct AnimationBlendPair {
    std::size_t first = 0, second = 0;
    double weight = 0;
};
struct AnimationBlendSelection {
    std::array<std::size_t, 3> samples{};
    std::array<double, 3> weights{};
    std::size_t count = 0;
};
struct AnimationBlendSpace {
    AssetId asset, skeleton;
    double cycle_seconds = 1;
    std::vector<AnimationBlendSample> samples;
    unsigned dimensions = 1;
    // Derived once at admission, never another authored geometry authority.
    std::array<double, 2> origin{};
    double extent = 1;
    std::vector<std::array<double, 2>> points;
    std::vector<std::array<std::size_t, 3>> triangles;
    std::vector<std::array<std::size_t, 2>> boundary;
    // Samples are admitted and sorted once; binding chooses at most two neighbors.
    AnimationBlendPair select(double position) const;
    AnimationBlendSelection select(double x, double y) const;
    std::size_t resident_bytes() const;
};
struct AnimationBlendSpaceSource {
    nlohmann::json document;
    static AnimationBlendSpaceSource create(AssetId, unsigned dimensions = 1);
    static AnimationBlendSpaceSource parse(std::span<const std::byte>);
    static AnimationBlendSpaceSource parse_cooked(std::span<const std::byte>,
                                                  unsigned wrapper_version = 0);
    AssetId asset() const;
    void validate() const;
    AnimationBlendSpace compiled() const;
    std::vector<std::byte> cook(unsigned wrapper_version = 0) const;
};
AnimationBlendSpace decode_animation_blend_space(std::span<const std::byte>, AssetId,
                                                 unsigned wrapper_version = 0);
} // namespace forge
