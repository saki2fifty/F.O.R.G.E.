#pragma once
#include <chrono>
#include <forge/animation.hpp>
#include <forge/audio.hpp>
#include <forge/input.hpp>
#include <forge/module.hpp>
#include <forge/physics.hpp>
#include <forge/scene.hpp>
#include <functional>
namespace forge {
struct RuntimeConfig {
    double simulation_hz = 60;
    double max_elapsed = .250;
    unsigned max_ticks = 8;
    void validate() const;
};
// Runtime owns time. Supplying a time point makes policy tests independent of sleeping.
class RuntimeClock {
  public:
    using Clock = std::chrono::steady_clock;
    using Time = Clock::time_point;
    using Tick = std::function<void(float)>;
    explicit RuntimeClock(RuntimeConfig config = {});
    void resume(Time now);
    void pause(Time now);
    void advance(Time now, const Tick& tick);
    void step(const Tick& tick);
    bool paused() const { return paused_; }
    double fixed_dt() const { return 1 / config_.simulation_hz; }
    double alpha() const { return paused_ ? 1 : accumulator_ / fixed_dt(); }
    std::uint64_t tick() const { return tick_; }
    Json status() const;
    void restore_tick(std::uint64_t tick, Time now) {
        tick_ = tick;
        pause(now);
        last_ticks_ = 0;
    }

  private:
    RuntimeConfig config_;
    Time previous_{};
    bool paused_ = true;
    double accumulator_ = 0, clamped_seconds_ = 0;
    std::uint64_t tick_ = 0, dropped_ticks_ = 0;
    unsigned last_ticks_ = 0;
};
// Derived samples keyed by generation-bearing Flecs handles; never authored ECS authority.
class PresentationPoses {
  public:
    using Nodes = std::map<std::uint64_t, TransformNode>;
    void reset(const Nodes& nodes);
    void capture(const Nodes& nodes);
    void snap(std::uint64_t entity);
    std::map<std::uint64_t, EvaluatedTransform> evaluate(double alpha) const;

  private:
    Nodes previous_, current_;
};
class RuntimeSimulation {
  public:
    RuntimeSimulation(WorldContext& context, Scene& scene, Module& module);
    ~RuntimeSimulation();
    RuntimeSimulation(const RuntimeSimulation&) = delete;
    RuntimeSimulation& operator=(const RuntimeSimulation&) = delete;
    void tick(float dt);
    void audio_paused(bool value);
    void sync_audio();
    RuntimeInput& input() { return input_; }
    Json input_status() const { return input_monitor_.status(input_.map()); }
    void reset_presentation();
    void restore_input_tick(std::uint64_t tick) {
        input_tick_ = tick;
        input_.release_all();
    }
    Json presentation(double alpha) const;

  private:
    WorldContext& context_;
    Scene& scene_;
    Module& module_;
    ForgeHostV1 host_;
    PresentationPoses poses_;
    RuntimeInput input_;
    InputMonitor input_monitor_;
    std::uint64_t input_tick_ = 0;
    flecs::entity previous_pipeline_;
    flecs::entity input_phase_, input_system_;
    flecs::entity pipeline_, gameplay_, transforms_, gameplay_phase_, transform_phase_;
    flecs::entity pre_physics_, physics_step_, physics_adopt_;
    flecs::entity pre_phase_, physics_phase_, adoption_phase_, post_phase_;
    std::shared_ptr<PhysicsRuntime> physics_;
    std::shared_ptr<AudioRuntime> audio_;
    std::shared_ptr<AnimationRuntime> animation_;
    std::exception_ptr stage_error_;
    template <class F> void stage(F&& f) noexcept {
        if (stage_error_)
            return;
        try {
            f();
        } catch (...) {
            stage_error_ = std::current_exception();
        }
    }
};
} // namespace forge
