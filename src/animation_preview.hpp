#pragma once
#include <forge/animation.hpp>
#include <forge/scene.hpp>
namespace forge {
// Editor-only host consumer: one copied runtime world, no physics or gameplay systems.
// All sampled poses and transitions are evaluated by the normal AnimationRuntime.
class SkeletalAnimationPreview {
  public:
    void start(const std::filesystem::path& project, Json source, EntityRef target);
    void stop() { world_.reset(); }
    bool active() const { return bool(world_); }
    bool prepare();
    void configure(float weight, float seconds, bool loop, float speed);
    void select_state(std::uint32_t);
    void select_blend_position(float);
    void select_blend_position(float x, float y);
    void seek(double time);
    void tick(float seconds);
    Json snapshot();
    Json pose();

  private:
    struct PreviewWorld {
        EngineContext engine;
        Scene scene;
        std::shared_ptr<AnimationRuntime> animation;
        EntityRef target;
        bool ready = false;
        PreviewWorld(const std::filesystem::path& project, const Json& source, EntityRef target);
        flecs::entity actor();
    };
    std::unique_ptr<PreviewWorld> world_;
};
} // namespace forge
