#pragma once
#include <forge/animation_components.hpp>
#include <forge/assets.hpp>
#include <memory>
#include <stop_token>
namespace forge {
// Prepared on a worker; catalog publication on the project-owning thread only.
// Dropping a candidate removes its unpublished staging files.
class AnimationCandidate {
  public:
    AnimationCandidate(AnimationCandidate&&) noexcept;
    AnimationCandidate& operator=(AnimationCandidate&&) noexcept;
    ~AnimationCandidate();
    std::vector<AssetRecord> publish();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit AnimationCandidate(std::unique_ptr<Impl>);
    friend AnimationCandidate prepare_animation_conversion(const std::filesystem::path&,
                                                           const std::filesystem::path&,
                                                           const std::filesystem::path&,
                                                           std::stop_token);
};
// Exact executable path supplied by application/package. Never searches PATH.
AnimationCandidate prepare_animation_conversion(const std::filesystem::path& project,
                                                const std::filesystem::path& source,
                                                const std::filesystem::path& converter,
                                                std::stop_token cancel = {});
} // namespace forge
