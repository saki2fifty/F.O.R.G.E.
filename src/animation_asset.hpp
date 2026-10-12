#pragma once
#include "animation_archive.hpp"
#include <array>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>
namespace forge::animation_detail {
inline constexpr const char* ozz_revision = "744eb9d99f606eda849acb0b1204f7a3dc20bca1";
using Matrix = std::array<float, 16>; // Column-major, skeleton model space.
struct LocalPose {
    std::array<float, 3> translation{}, scale{1, 1, 1};
    std::array<float, 4> rotation{0, 0, 0, 1};
};
// Instances can only be constructed through admission. Immutable after construction.
class Skeleton {
  public:
    explicit Skeleton(std::span<const std::byte>);
    ~Skeleton();
    Skeleton(const Skeleton&) = delete;
    const ArchiveInfo& info() const { return info_; }
    std::size_t resident_bytes() const;
    std::vector<std::string> joint_names() const;
    // Evaluated through native LocalToModelJob after archive admission.
    std::vector<Matrix> rest_pose() const;
    LocalPose joint_rest_pose(std::size_t joint) const;

  private:
    friend class Sampler;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    ArchiveInfo info_;
};
class Clip {
  public:
    explicit Clip(std::span<const std::byte>);
    ~Clip();
    Clip(const Clip&) = delete;
    const ArchiveInfo& info() const { return info_; }
    std::size_t resident_bytes() const;

  private:
    friend class Sampler;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    ArchiveInfo info_;
};
class Sampler {
  public:
    explicit Sampler(std::shared_ptr<const Skeleton>, std::shared_ptr<const Clip>);
    ~Sampler();
    Sampler(const Sampler&) = delete;
    // Per-Animator sampling context, never shared mutable playback state.
    std::vector<Matrix> sample(float ratio);
    // Native Ozz local-space blending, before the single local-to-model pass.
    std::vector<Matrix> sample_blended(float ratio, Sampler& second, float weight);
    // State transitions use independent clip phases. Interrupted transitions can
    // blend from an already evaluated local pose without another sampling layer.
    std::vector<Matrix> sample_transition(float ratio, Sampler& outgoing, float outgoing_ratio,
                                          float weight);
    std::vector<Matrix> sample_transition(float ratio, std::span<const LocalPose> from,
                                          float weight);
    // Copied directly from native sampled SoA lanes; valid after successful sample.
    // No matrix decomposition, including at zero or signed scale.
    std::vector<LocalPose> local_pose() const;
    // Capture the unblended layer root and remove its translation/rotation before
    // the existing single LocalToModel pass. Animated scale remains unchanged.
    void root_motion_joint(std::optional<std::size_t> joint);
    std::optional<LocalPose> sampled_root() const;
    std::uint64_t sampling_jobs() const;
    void reset();

  private:
    void sample_local(float ratio);
    std::vector<Matrix> evaluate_model();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace forge::animation_detail
