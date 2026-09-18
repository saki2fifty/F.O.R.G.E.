#pragma once
#include <forge/asset_ref.hpp>
namespace forge {
struct SkeletonAsset {
    static constexpr const char* type = "skeleton";
};
struct AnimationClipAsset {
    static constexpr const char* type = "animation_clip";
};
struct Animator {
    AssetRef<SkeletonAsset> skeleton{};
    AssetRef<AnimationClipAsset> clip{};
    bool enabled = true, play_on_start = true, loop = true;
    float playback_speed = 1;
    bool operator==(const Animator&) const = default;
};
} // namespace forge
