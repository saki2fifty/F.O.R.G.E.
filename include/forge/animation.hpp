#pragma once
#include <forge/animation_components.hpp>
#include <forge/animation_service.hpp>
#include <forge/world.hpp>
#include <functional>
namespace forge {
class AssetCatalog;
EngineModule animation_module(std::filesystem::path project);
class AnimationRuntime : public AnimationService {
  public:
    AnimationRuntime(WorldContext&, std::filesystem::path project);
    ~AnimationRuntime();
    // Host composition only: validate a complete model pose before writes and
    // recovery admission. No solver ownership or native pointers cross the SDK.
    using PoseValidator = std::function<void(const std::map<std::uint64_t, TransformNode>&)>;
    void pose_validator(PoseValidator);
    void synchronize();
    // Unpublished scene only: admit initial assets and apply the current sample
    // without advancing time or running gameplay. False means workers still pending.
    bool prepare_initial_pose();
    // Editor copied-world command: admit a named-state selection without advancing clocks.
    void prepare_preview_state();
    // Development host notifications. Catalog IO runs off-thread; sampling/adoption
    // remains on the world owner. Neither operation edits the authored hierarchy.
    void refresh_assets();
    void catalog(std::shared_ptr<const AssetCatalog>);
    void tick(float dt, std::uint64_t tick = 0);
    std::span<const AnimationEvent> events() const override;
    std::optional<AnimationPlayback> playback(EntityRef) const override;
    std::optional<AnimationRootMotion> root_motion(EntityRef) const override;
    bool control(EntityRef, AnimationControl, std::uint64_t current_tick) override;
    // Host-owned copied editor previews never deliver gameplay markers.
    void event_delivery(bool enabled);
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
// Private host bridge to the module-owned runtime; absent from the SDK.
std::shared_ptr<AnimationRuntime> animation_runtime(WorldContext&);
} // namespace forge
