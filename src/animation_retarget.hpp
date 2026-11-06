#pragma once
#include "animation_asset.hpp"
#include "animation_root_motion.hpp"
#include <forge/animation_components.hpp>
#include <forge/assets.hpp>
#include <nlohmann/json.hpp>
#include <stop_token>

namespace forge {
inline constexpr std::size_t animation_retarget_byte_limit = 256 * 1024;
inline constexpr std::size_t animation_retarget_joint_limit = 256;
inline constexpr std::size_t animation_retarget_sample_limit = 32768;
bool is_animation_retarget_source(const AssetRecord&);
struct AnimationRetargetEntry {
    AssetId source, target;
};
struct AnimationRetargetSettings {
    AssetId source_skeleton, source_clip, target_skeleton;
    std::vector<AnimationRetargetEntry> entries;
    unsigned sampling_rate = 30;
    bool root_translation = false;
    double translation_scale = 1;
    std::array<double, 4> alignment{0, 0, 0, 1};
};
struct AnimationRetargetSource {
    nlohmann::json document;
    static AnimationRetargetSource create(AssetId);
    static AnimationRetargetSource parse(std::span<const std::byte>);
    AssetId asset() const;
    void validate() const;
    AnimationRetargetSettings settings() const;
};
namespace animation_detail {
struct RetargetRig {
    std::vector<AssetId> joints;
    std::vector<std::int16_t> parents;
    std::vector<LocalPose> rest;
};
// Disposable compiled indices for two selected immutable skeleton revisions.
// No names, source array indices or runtime handles become persistent identity.
class RetargetPose {
  public:
    RetargetPose(RetargetRig source, RetargetRig target, const AnimationRetargetSettings&);
    std::vector<LocalPose> sample(std::span<const LocalPose>, std::stop_token = {}) const;
    const RetargetRig& target() const { return target_; }

  private:
    RetargetRig source_, target_;
    std::vector<RootRigid> source_rest_, target_rest_;
    std::vector<int> mapping_;
    RootRigid alignment_;
    int translation_joint_ = -1;
    double translation_scale_ = 1;
};
// Owned samples are prepared before calling the exact pinned offline builder.
struct SampledAnimation {
    std::vector<float> times;
    std::vector<std::vector<LocalPose>> poses;
};
unsigned retarget_sample_intervals(float duration, unsigned sampling_rate,
                                   std::size_t source_joints, std::size_t target_joints);
std::vector<std::byte> build_sampled_animation(const SampledAnimation&, std::stop_token = {});
} // namespace animation_detail
} // namespace forge
