#pragma once
#include "animation_archive.hpp"
#include <array>
#include <memory>
#include <span>
#include <string>
#include <vector>
namespace forge::animation_detail {
inline constexpr const char* ozz_revision = "744eb9d99f606eda849acb0b1204f7a3dc20bca1";
using Matrix = std::array<float, 16>; // Column-major, skeleton model space.
// Instances can only be constructed through admission. Immutable after construction.
class Skeleton {
  public:
    explicit Skeleton(std::span<const std::byte>);
    ~Skeleton();
    Skeleton(const Skeleton&) = delete;
    const ArchiveInfo& info() const { return info_; }
    std::vector<std::string> joint_names() const;
    // Evaluated through native LocalToModelJob after archive admission.
    std::vector<Matrix> rest_pose() const;

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
    void reset();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace forge::animation_detail
