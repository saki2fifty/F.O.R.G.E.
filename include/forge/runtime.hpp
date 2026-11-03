#pragma once
#include <chrono>
#include <forge/animation.hpp>
#include <forge/audio.hpp>
#include <forge/input.hpp>
#include <forge/navigation.hpp>
#include <forge/physics.hpp>
#include <forge/scene.hpp>
#include <functional>
namespace forge {
namespace detail {
class InputMessageRuntime;
}
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
    // Real-time observation only. Equal timestamps keep the last interval/sequence;
    // neither Pause nor simulation scaling changes this transient timeline.
    void sample_time(Time now);
    void advance(Time now, const Tick& tick);
    void step(const Tick& tick);
    // Frame-boundary policy, not a change to the physics/fixed simulation delta.
    void set_time_scale(double value);
    double time_scale() const { return time_scale_; }
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
    Time unscaled_origin_{}, unscaled_previous_{};
    bool time_sampled_ = false;
    double unscaled_delta_ = 0;
    std::uint64_t unscaled_sequence_ = 0;
    bool paused_ = true;
    double accumulator_ = 0, clamped_seconds_ = 0;
    double time_scale_ = 1;
    std::uint64_t tick_ = 0, dropped_ticks_ = 0;
    unsigned last_ticks_ = 0;
};
// Derived samples keyed by generation-bearing Flecs handles; never authored ECS authority.
class PresentationPoses {
  public:
    using Nodes = std::map<std::uint64_t, TransformNode>;
    void reset(const Nodes& nodes);
    void capture(const Nodes& nodes);
    void capture_owned(Nodes nodes);
    void snap(std::uint64_t entity);
    void snap(std::uint64_t entity, const TransformNode& current);
    std::map<std::uint64_t, EvaluatedTransform> evaluate(double alpha) const;

  private:
    Nodes previous_, current_;
};
class ParticleRuntime;
class RuntimeSimulation {
  public:
    RuntimeSimulation(WorldContext& context, Scene& scene);
    ~RuntimeSimulation();
    RuntimeSimulation(const RuntimeSimulation&) = delete;
    RuntimeSimulation& operator=(const RuntimeSimulation&) = delete;
    void tick(float dt);
    void audio_paused(bool value);
    void activate_audio(bool game_paused);
    void suspend_audio();
    void sync_audio();
    // Tickless retirement; preserves surviving solver poses and queued commands.
    void reconcile_state_conditions();
    // Publish privately edited animation joints without changing other interpolation.
    void sync_animation_pose() const;
    RuntimeInput& input() { return input_; }
    Json input_status() const;
    void reset_presentation();
    void restore_input_tick(std::uint64_t tick) {
        input_tick_ = tick;
        input_.release_all();
    }
    Json presentation(double alpha) const;
    // Last collected native fixed-pipeline plan; requests a fresh observation on
    // the next collected fixed tick. Never progresses gameplay. Native system
    // CPU counters retain lifetime semantics across collection/Clear.
    Json system_profile() const;

  private:
    WorldContext& context_;
    Scene& scene_;
    mutable PresentationPoses poses_;
    RuntimeInput input_;
    InputMonitor input_monitor_;
    std::unique_ptr<detail::InputMessageRuntime> messages_;
    std::uint64_t input_tick_ = 0;
    Json system_profile_ = Json::object();
    std::uint64_t system_profile_epoch_ = 0;
    mutable bool system_profile_requested_ = false;
    void capture_system_profile();
    flecs::entity previous_pipeline_;
    flecs::entity input_phase_, input_system_, message_phase_, message_system_;
    flecs::entity pipeline_, transforms_, gameplay_phase_, transform_phase_;
    flecs::entity pre_physics_, physics_step_, physics_adopt_;
    flecs::entity pre_phase_, physics_phase_, adoption_phase_, post_phase_;
    std::shared_ptr<PhysicsRuntime> physics_;
    std::shared_ptr<AudioRuntime> audio_;
    std::shared_ptr<AnimationRuntime> animation_;
    std::shared_ptr<ParticleRuntime> particles_;
    flecs::entity animation_phase_, animation_system_;
    flecs::entity post_animation_phase_;
    std::shared_ptr<NavigationRuntime> navigation_;
    flecs::entity navigation_phase_, navigation_system_;
    std::function<void()> previous_state_retirement_;
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
