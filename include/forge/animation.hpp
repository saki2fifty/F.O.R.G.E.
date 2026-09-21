#pragma once
#include <forge/animation_components.hpp>
#include <forge/world.hpp>
#include <functional>
namespace forge {
EngineModule animation_module(std::filesystem::path project);
class AnimationRuntime {
  public:
    AnimationRuntime(WorldContext&, std::filesystem::path project);
    ~AnimationRuntime();
    // Host composition only: validate a complete model pose before writes and
    // recovery admission. No solver ownership or native pointers cross the SDK.
    using PoseValidator = std::function<void(const std::map<std::uint64_t, TransformNode>&)>;
    void pose_validator(PoseValidator);
    void synchronize();
    void tick(float dt);
    void reset_presentation();
    Json presentation(flecs::entity_t entity, double alpha);
    // Resource sampling can be ready while a newly loaded model still awaits
    // its first fixed-boundary channel application. Visual hosts must distinguish
    // those states without mutating transforms during presentation extraction.
    bool model_pose_ready(flecs::entity_t entity) const;
    std::vector<std::uint64_t> take_discontinuities();
    bool checkpoint_ready() const;
    Json checkpoint() const;
    void restore(const Json&);
    void shutdown() noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// Private host bridge, absent from SDK. Does not publish an animation service API.
std::shared_ptr<AnimationRuntime> animation_runtime(WorldContext&);
} // namespace forge
