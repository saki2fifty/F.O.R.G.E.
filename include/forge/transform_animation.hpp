#pragma once
#include <forge/animation_components.hpp>
#include <forge/transform.hpp>
#include <nlohmann/json.hpp>
#include <span>
#include <vector>
namespace forge {
inline constexpr std::size_t transform_animation_byte_limit = 2 * 1024 * 1024;
inline constexpr std::int64_t animation_timebase = 1000000;
struct TransformAnimationKey {
    AssetId id;
    std::int64_t time = 0;
    LocalTransform pose;
    bool held = false;
};
struct TransformAnimationClip {
    AssetId asset;
    std::int64_t duration = 0;
    unsigned channels = 7;
    std::vector<TransformAnimationKey> keys;
    LocalTransform sample(double microseconds) const;
    std::size_t resident_bytes() const;
};
struct TransformAnimationSource {
    nlohmann::json document;
    static TransformAnimationSource create(AssetId);
    static TransformAnimationSource parse(std::span<const std::byte>);
    AssetId asset() const;
    void validate() const;
    TransformAnimationClip clip() const;
    std::vector<std::byte> cook() const;
    AssetId set_key(std::int64_t time, const LocalTransform&, bool held = false, AssetId key = {});
    void erase_key(AssetId);
    void move_key(AssetId, std::int64_t time);
};
TransformAnimationClip decode_transform_animation(std::span<const std::byte>, AssetId);
nlohmann::json animation_pose_json(const LocalTransform&);
LocalTransform animation_pose(const nlohmann::json&);
} // namespace forge
