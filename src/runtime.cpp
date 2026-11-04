#include "animation_component_values.hpp"
#include "flecs_system_trace.hpp"
#include "input_message_runtime.hpp"
#include "json_output_text.hpp"
#include "particle_runtime.hpp"
#include "runtime_entity_creation.hpp"
#include "runtime_tile_map.hpp"
#include "utf8_boundary.hpp"
#include <algorithm>
#include <cmath>
#include <forge/application_states.hpp>
#include <forge/render_components.hpp>
#include <forge/runtime.hpp>
#include <limits>
#include <stdexcept>
namespace forge {
void RuntimeConfig::validate() const {
    if (!std::isfinite(simulation_hz) || simulation_hz < 1 || simulation_hz > 240 ||
        !std::isfinite(max_elapsed) || max_elapsed <= 0 || max_elapsed > 1 || max_ticks < 1 ||
        max_ticks > 64)
        throw std::runtime_error(
            "Runtime requires 1..240 Hz, elapsed clamp in (0,1], 1..64 catch-up ticks");
}
RuntimeClock::RuntimeClock(RuntimeConfig config) : config_(config) { config_.validate(); }
void RuntimeClock::resume(Time now) {
    previous_ = now;
    accumulator_ = 0;
    paused_ = false;
    presentation_floor_tick_ = tick_;
}
void RuntimeClock::pause(Time now) {
    previous_ = now;
    accumulator_ = 0;
    paused_ = true;
    presentation_floor_tick_ = tick_;
}
void RuntimeClock::sample_time(Time now) {
    if (!time_sampled_) {
        unscaled_origin_ = unscaled_previous_ = now;
        time_sampled_ = true;
        return;
    }
    if (now < unscaled_previous_)
        throw std::runtime_error("Runtime observation clock must be monotonic");
    if (now == unscaled_previous_)
        return;
    if (unscaled_sequence_ == std::numeric_limits<std::uint64_t>::max())
        throw std::runtime_error("Runtime observation sequence exhausted");
    unscaled_delta_ = std::chrono::duration<double>(now - unscaled_previous_).count();
    unscaled_previous_ = now;
    ++unscaled_sequence_;
}
void RuntimeClock::advance(Time now, const Tick& simulate) {
    if (now < previous_)
        throw std::runtime_error("Runtime clock must be monotonic");
    sample_time(now);
    last_ticks_ = 0;
    double elapsed = std::chrono::duration<double>(now - previous_).count();
    previous_ = now;
    if (paused_)
        return;
    clamped_seconds_ += std::max(0.0, elapsed - config_.max_elapsed);
    accumulator_ += std::min(elapsed, config_.max_elapsed) * time_scale_;
    const auto dt = fixed_dt();
    // Only absorb floating-point summation error, not a meaningful fraction of a tick.
    const double epsilon = dt * 1e-10;
    while (accumulator_ + epsilon >= dt && last_ticks_ < config_.max_ticks) {
        simulate(static_cast<float>(dt));
        ++tick_;
        ++last_ticks_;
        accumulator_ = std::max(0.0, accumulator_ - dt);
    }
    const auto debt = static_cast<std::uint64_t>((accumulator_ + epsilon) / dt);
    dropped_ticks_ += debt;
    accumulator_ = std::max(0.0, accumulator_ - double(debt) * dt);
}
void RuntimeClock::step(const Tick& simulate) {
    if (!paused_)
        throw std::runtime_error("Single Step requires Pause");
    simulate(static_cast<float>(fixed_dt()));
    ++tick_;
    last_ticks_ = 1;
    accumulator_ = 0;
}
Json RuntimeClock::status() const {
    return {{"paused", paused_},
            {"simulation_hz", config_.simulation_hz},
            {"fixed_dt", fixed_dt()},
            {"time_scale", time_scale_},
            {"unscaled_time",
             time_sampled_
                 ? std::chrono::duration<double>(unscaled_previous_ - unscaled_origin_).count()
                 : 0.0},
            {"unscaled_delta", unscaled_delta_},
            {"unscaled_sequence", unscaled_sequence_},
            {"tick", tick_},
            {"ticks_this_iteration", last_ticks_},
            {"accumulator", accumulator_},
            {"alpha", alpha()},
            {"presentation_seconds", presentation_seconds()},
            {"dropped_ticks", dropped_ticks_},
            {"clamped_seconds", clamped_seconds_}};
}
double RuntimeClock::presentation_seconds() const {
    const auto interval = tick_ ? double(tick_ - 1) + alpha() : 0;
    return std::max(double(presentation_floor_tick_), interval) * fixed_dt();
}
void RuntimeClock::set_time_scale(double value) {
    if (!std::isfinite(value) || value < 0 || value > 4)
        throw std::runtime_error("Simulation time scale must be finite and in [0,4]");
    time_scale_ = value;
}
namespace {
LocalRotation slerp(LocalRotation a, LocalRotation b, double t) {
    a = normalized(a);
    b = normalized(b);
    double cosine = double(a.x) * b.x + double(a.y) * b.y + double(a.z) * b.z + double(a.w) * b.w;
    if (cosine < 0) {
        b = {-b.x, -b.y, -b.z, -b.w};
        cosine = -cosine;
    }
    double left = 1 - t, right = t;
    if (cosine < .9995) {
        const double angle = std::acos(std::clamp(cosine, 0.0, 1.0));
        left = std::sin((1 - t) * angle) / std::sin(angle);
        right = std::sin(t * angle) / std::sin(angle);
    }
    return normalized({float(left * a.x + right * b.x), float(left * a.y + right * b.y),
                       float(left * a.z + right * b.z), float(left * a.w + right * b.w)});
}
} // namespace
void PresentationPoses::reset(const Nodes& nodes) { previous_ = current_ = nodes; }
void PresentationPoses::capture(const Nodes& nodes) { capture_owned(nodes); }
void PresentationPoses::capture_owned(Nodes nodes) {
    previous_ = std::move(current_);
    current_ = std::move(nodes);
    auto before = previous_.begin();
    for (const auto& [id, node] : current_) {
        while (before != previous_.end() && before->first < id)
            before = previous_.erase(before);
        if (before == previous_.end() || id < before->first)
            previous_.emplace_hint(before, id, node);
        else {
            if (before->second.parent != node.parent ||
                before->second.parent_resolved != node.parent_resolved)
                before->second = node;
            ++before;
        }
    }
    previous_.erase(before, previous_.end());
}
void PresentationPoses::snap(std::uint64_t entity) {
    if (current_.contains(entity))
        previous_[entity] = current_.at(entity);
}
void PresentationPoses::snap(std::uint64_t entity, const TransformNode& current) {
    previous_[entity] = current;
    current_[entity] = current;
}
std::map<std::uint64_t, EvaluatedTransform> PresentationPoses::evaluate(double alpha) const {
    if (!std::isfinite(alpha) || alpha < 0 || alpha > 1)
        throw std::runtime_error("Presentation alpha must be between 0 and 1");
    auto nodes = current_;
    for (auto& [id, node] : nodes) {
        const auto& old = previous_.at(id).local;
        auto& value = node.local;
        value.translation = {std::lerp(old.translation.x, value.translation.x, alpha),
                             std::lerp(old.translation.y, value.translation.y, alpha),
                             std::lerp(old.translation.z, value.translation.z, alpha)};
        value.scale = {float(std::lerp(double(old.scale.x), double(value.scale.x), alpha)),
                       float(std::lerp(double(old.scale.y), double(value.scale.y), alpha)),
                       float(std::lerp(double(old.scale.z), double(value.scale.z), alpha))};
        value.rotation = slerp(old.rotation, value.rotation, alpha);
    }
    return evaluate_transforms(nodes);
}
RuntimeSimulation::RuntimeSimulation(WorldContext& context, Scene& scene,
                                     RuntimeExecutionConfig execution)
    : context_(context), scene_(scene) {
    execution.validate();
    if (context.role() != WorldRole::Runtime)
        throw std::runtime_error("Simulation requires a runtime WorldContext");
    if (!context.on_owner_thread() || context.world().is_readonly())
        throw std::runtime_error("Simulation requires the writable runtime owner thread");
    if (execution.ecs_workers > 1 &&
        (context.world().get_threads() != 1 || ecs_using_task_threads(context.world())))
        throw std::runtime_error("Fixed ECS workers already have an external configuration owner");
    if (execution.ecs_workers > 1 && !ecs_os_has_threading())
        throw std::runtime_error("Native fixed ECS workers are unavailable on this host");
    animation_ = animation_runtime(context);
    particles_ = particle_runtime(context);
    if (context.services().available(Capability::Navigation)) {
        navigation_ = std::static_pointer_cast<NavigationRuntime>(context.services().navigation());
        navigation_->bind(&scene);
    }

    auto& world = context.world();
    previous_pipeline_ = world.get_pipeline();
    // The built-in timer systems consume exactly this pipeline's explicit fixed
    // delta. They are never progressed from editor/presentation wall time.
    for (const char* name : {"flecs.timer.ProgressTimers", "flecs.timer.ProgressRateFilters",
                             "flecs.timer.ProgressTickSource"}) {
        auto system = world.entity(ecs_lookup(world, name));
        if (!system)
            throw std::runtime_error("Pinned Flecs timer system is unavailable");
        system.add<FixedSimulation>();
    }
    input_phase_ =
        world.entity("forge.runtime.Input").add(flecs::Phase).depends_on(flecs::PreFrame);
    message_phase_ =
        world.entity("forge.runtime.Messages").add(flecs::Phase).depends_on(input_phase_);
    gameplay_phase_ =
        world.entity("forge.runtime.Gameplay").add(flecs::Phase).depends_on(message_phase_);
    input_system_ = world.system("forge.runtime.InputMonitor")
                        .kind(input_phase_)
                        .immediate()
                        .run([this](flecs::iter&) { input_monitor_.consume(input_.snapshot()); });
    messages_ = std::make_unique<detail::InputMessageRuntime>(context_, scene_);
    message_system_ = world.system("forge.runtime.InputMessages")
                          .kind(message_phase_)
                          .immediate()
                          .run([this](flecs::iter&) {
                              stage([&] { messages_->tick(input_.snapshot(), input_.map()); });
                          });
    if (context.services().available(Capability::Physics))
        physics_ = std::static_pointer_cast<PhysicsRuntime>(context.services().physics());
    if (animation_) {
        if (physics_)
            animation_->pose_validator(
                [weak = std::weak_ptr<PhysicsRuntime>(physics_)](const auto& candidate) {
                    const auto physics = weak.lock();
                    if (!physics)
                        throw std::runtime_error("Animation physics validation owner expired");
                    physics->validate_transform_candidate(candidate);
                });
        else
            animation_->pose_validator({});
    }
    if (context.services().available(Capability::Audio))
        audio_ = std::static_pointer_cast<AudioRuntime>(context.services().audio());
    animation_phase_ =
        world.entity("forge.runtime.Animation").add(flecs::Phase).depends_on(gameplay_phase_);
    animation_system_ = world.system("forge.runtime.AnimationUpdate")
                            .kind(animation_phase_)
                            .immediate()
                            .run([this](flecs::iter& it) {
                                if (animation_)
                                    stage([&] {
                                        auto profile = context_.services().profile(
                                            "runtime", "Animation", input_tick_);
                                        animation_->tick(it.delta_time(), input_tick_);
                                    });
                            });
    animation_system_.add<FixedSimulation>();
    post_animation_phase_ =
        world.entity("forge.runtime.PostAnimation").add(flecs::Phase).depends_on(animation_phase_);
    navigation_phase_ = world.entity("forge.runtime.Navigation")
                            .add(flecs::Phase)
                            .depends_on(post_animation_phase_);
    navigation_system_ = world.system("forge.runtime.NavigationUpdate")
                             .kind(navigation_phase_)
                             .immediate()
                             .run([this](flecs::iter& it) {
                                 if (navigation_)
                                     stage([&] {
                                         auto profile = context_.services().profile(
                                             "runtime", "Navigation", input_tick_);
                                         navigation_->tick(it.delta_time(), input_tick_);
                                     });
                             });
    navigation_system_.add<FixedSimulation>();
    pre_phase_ =
        world.entity("forge.runtime.PrePhysics").add(flecs::Phase).depends_on(navigation_phase_);
    physics_phase_ = world.entity("forge.runtime.Physics").add(flecs::Phase).depends_on(pre_phase_);
    adoption_phase_ =
        world.entity("forge.runtime.PhysicsAdoption").add(flecs::Phase).depends_on(physics_phase_);
    post_phase_ =
        world.entity("forge.runtime.PostPhysics").add(flecs::Phase).depends_on(adoption_phase_);
    transform_phase_ =
        world.entity("forge.runtime.Transforms").add(flecs::Phase).depends_on(post_phase_);
    pre_physics_ = world.system("forge.runtime.PhysicsSynchronization")
                       .kind(pre_phase_)
                       .immediate()
                       .run([this](flecs::iter& it) {
                           if (physics_)
                               stage([&] {
                                   auto profile = context_.services().profile(
                                       "runtime", "PhysicsSynchronization", input_tick_);
                                   physics_->synchronize(it.delta_time());
                               });
                       });
    physics_step_ = world.system("forge.runtime.PhysicsStep")
                        .kind(physics_phase_)
                        .immediate()
                        .run([this](flecs::iter& it) {
                            if (physics_)
                                stage([&] {
                                    auto profile = context_.services().profile(
                                        "runtime", "PhysicsStep", input_tick_);
                                    physics_->step(it.delta_time());
                                });
                        });
    physics_adopt_ = world.system("forge.runtime.PhysicsAdoptionSystem")
                         .kind(adoption_phase_)
                         .immediate()
                         .run([this](flecs::iter&) {
                             if (physics_)
                                 stage([&] {
                                     auto profile = context_.services().profile(
                                         "runtime", "PhysicsAdoption", input_tick_);
                                     physics_->adopt();
                                 });
                         });
    pre_physics_.add<FixedSimulation>();
    physics_step_.add<FixedSimulation>();
    physics_adopt_.add<FixedSimulation>();
    pipeline_ = world.pipeline()
                    .with(flecs::System)
                    .with<FixedSimulation>()
                    .with(flecs::Phase)
                    .cascade(flecs::DependsOn)
                    .build();
    transforms_ =
        world.system("forge.runtime.FinalTransforms")
            .kind(transform_phase_)
            .immediate()
            .run([this](flecs::iter&) { stage([&] { context_.evaluate_world_transforms(); }); });
    world.set_pipeline(pipeline_);
    input_system_.add<FixedSimulation>();
    message_system_.add<FixedSimulation>();
    transforms_.add<FixedSimulation>();
    reset_presentation();
    previous_state_retirement_ = exchange_application_state_retirement_handler(
        world, [this] { reconcile_state_conditions(); });
    // The pinned native per-system counter is shared across worker callbacks.
    // Keep it disabled while stages can run concurrently; host scopes remain valid.
    if (execution.ecs_workers > 1 || world.get_threads() > 1) {
        previous_system_timing_ = (ecs_world_get_flags(world) & EcsWorldMeasureSystemTime) != 0;
        if (ecs_os_has_time())
            ecs_measure_system_time(world, false);
        system_timing_suppressed_ = true;
    }
    if (execution.ecs_workers > 1) {
        worker_trace_ = detail::make_native_worker_trace_domain(execution.ecs_workers);
        detail::NativeWorkerTraceStartup trace_startup(worker_trace_.get());
        world.set_threads(static_cast<int>(execution.ecs_workers));
        owns_workers_ = true;
    }
}
void RuntimeSimulation::reconcile_state_conditions() {
    if (physics_)
        physics_->retire_destroyed_entities(true);
    if (audio_)
        audio_->refresh_sources();
}
RuntimeSimulation::~RuntimeSimulation() {
    // Join owned stages before retiring systems, providers or module code leases.
    if (owns_workers_)
        context_.world().set_threads(1);
    if (system_timing_suppressed_ && ecs_os_has_time())
        ecs_measure_system_time(context_.world(), previous_system_timing_);
    exchange_application_state_retirement_handler(context_.world(),
                                                  std::move(previous_state_retirement_));
    context_.world().set_pipeline(previous_pipeline_);
    for (const char* name : {"flecs.timer.ProgressTimers", "flecs.timer.ProgressRateFilters",
                             "flecs.timer.ProgressTickSource"})
        context_.world().entity(ecs_lookup(context_.world(), name)).remove<FixedSimulation>();
    if (navigation_)
        navigation_->bind(nullptr);
    if (animation_)
        animation_->pose_validator({});
    navigation_system_.destruct();
    navigation_phase_.destruct();
    post_animation_phase_.destruct();
    animation_system_.destruct();
    animation_phase_.destruct();
    pre_physics_.destruct();
    physics_step_.destruct();
    physics_adopt_.destruct();
    input_system_.destruct();
    message_system_.destruct();
    transforms_.destruct();
    pipeline_.destruct();
    transform_phase_.destruct();
    post_phase_.destruct();
    adoption_phase_.destruct();
    physics_phase_.destruct();
    pre_phase_.destruct();
    gameplay_phase_.destruct();
    message_phase_.destruct();
    input_phase_.destruct();
}
Json RuntimeSimulation::input_status() const {
    auto status = input_monitor_.status(input_.map());
    status["rebind_listening"] = input_.rebinding();
    status["messages"] = messages_->status();
    return status;
}
void RuntimeSimulation::tick(float dt) {
    if (!context_.on_owner_thread() || context_.world().is_readonly())
        throw std::runtime_error("Gameplay tick requires the writable runtime owner thread");
    if (system_timing_suppressed_ && ecs_os_has_time())
        ecs_measure_system_time(context_.world(), false);
    if (!std::isfinite(dt) || dt <= 0)
        throw std::runtime_error("Gameplay requires a positive fixed tick delta");
    // progress updates Flecs frame/time metadata from this explicit fixed delta.
    auto profile = context_.services().profile("runtime", "FixedSimulationTick", input_tick_ + 1);
    stage_error_ = nullptr;
    input_.latch(++input_tick_);
    {
        auto profile =
            context_.services().profile("runtime", "RuntimeEntityPublication", input_tick_);
        detail::publish_runtime_entities(context_);
    }
    if (context_.services().available(Capability::Resources)) {
        auto profile =
            context_.services().profile("runtime", "ResourceSynchronization", input_tick_);
        context_.services().resources()->synchronize();
    }
    detail::synchronize_runtime_tile_maps(context_);
    detail::publish_runtime_tile_edits(context_);
    // Navigation observes the same admitted Map revision/cells used by this tick's
    // physics and gameplay. Edited baked sources must stop before agent movement.
    if (navigation_)
        navigation_->synchronize();
    context_.modules().begin_tick(input_.snapshot());
    try {
        {
            auto profile = context_.services().profile("runtime", "ApplicationStates", input_tick_);
            advance_application_states(context_.world());
        }
        detail::NativeSystemTraceCapture native_traces(context_.services(), input_tick_,
                                                       worker_trace_.get());
        context_.world().progress(dt);
    } catch (...) {
        context_.modules().end_tick();
        throw;
    }
    context_.modules().end_tick();
    if (stage_error_)
        std::rethrow_exception(stage_error_);
    if (system_profile_requested_ && context_.services().available(Capability::Profiling) &&
        context_.services().profiling_enabled())
        capture_system_profile();
    if (particles_) {
        auto profile = context_.services().profile("runtime", "Particles", input_tick_);
        particles_->tick(dt);
    }
    {
        auto profile =
            context_.services().profile("runtime", "PresentationPoseCapture", input_tick_);
        poses_.capture_owned(context_.transform_nodes());
    }
    auto discontinuities =
        animation_ ? animation_->take_discontinuities() : std::vector<std::uint64_t>{};
    if (physics_) {
        const auto physics_discontinuities = physics_->take_discontinuities();
        discontinuities.insert(discontinuities.end(), physics_discontinuities.begin(),
                               physics_discontinuities.end());
    }
    for (auto id : discontinuities)
        poses_.snap(id);
    if (audio_) {
        auto profile = context_.services().profile("runtime", "AudioSynchronization", input_tick_);
        audio_->synchronize(input_tick_, dt, discontinuities);
    }
    detail::advance_runtime_tile_animation(context_, dt);
}
Json RuntimeSimulation::system_profile() const {
    // This also checks service lifetime/capability/owner-thread admission.
    const auto epoch = context_.services().profile_epoch();
    system_profile_requested_ = true;
    if (system_profile_.empty() || epoch != system_profile_epoch_)
        return {{"available", false},
                {"reason", "Start CPU collection and advance a fixed tick to inspect systems."}};
    return system_profile_;
}
void RuntimeSimulation::capture_system_profile() {
    system_profile_requested_ = false;
    system_profile_epoch_ = context_.services().profile_epoch();
    try {
        struct PipelineStats {
            ecs_pipeline_stats_t value{};
            ~PipelineStats() { ecs_pipeline_stats_fini(&value); }
        } stats;
        auto& world = context_.world();
        // Native plan is built by real progress. Never build/progress it just for
        // inspection, or reduce its history (including empty authoring pipelines).
        if (!ecs_pipeline_stats_get(world, pipeline_, &stats.value))
            throw std::runtime_error("Native fixed-pipeline statistics are unavailable");
        constexpr int limit = 256;
        const auto count = ecs_vec_count(&stats.value.systems);
        const auto* systems = ecs_vec_first_t(&stats.value.systems, ecs_entity_t);
        Json rows = Json::array();
        const auto name = [&world](ecs_entity_t id) {
            std::string text = id ? world.entity(id).path().c_str() : "";
            if (text.size() > 160) {
                text.resize(detail::utf8_range_end(text, 0, 157));
                text += "...";
            }
            return detail::json_output_text(text);
        };
        for (int i = 0; i < std::min(count, limit); ++i) {
            const auto id = systems[i];
            if (!id) {
                rows.push_back({{"kind", "merge"}});
                continue;
            }
            const auto* system = ecs_system_get(world, id);
            if (!system)
                throw std::runtime_error("Native pipeline contains an unavailable system");
            const double seconds = system->time_spent;
            rows.push_back({{"kind", "system"},
                            {"id", id},
                            {"name", name(id)},
                            {"phase", name(ecs_get_target(world, id, EcsDependsOn, 0))},
                            {"cpu_seconds", !system_timing_suppressed_ && ecs_os_has_time() &&
                                                    std::isfinite(seconds) && seconds >= 0
                                                ? Json(seconds)
                                                : Json(nullptr)},
                            {"immediate", system->immediate},
                            {"multi_threaded", system->multi_threaded}});
        }
        system_profile_ = {{"available", true},
                           {"tick", input_tick_},
                           {"timing", "native_system_lifetime"},
                           {"capacity", limit},
                           {"total_entries", count},
                           {"truncated", count > limit},
                           {"entries", std::move(rows)}};
        if (context_.world().get_threads() > 1) {
            system_profile_["ecs_workers"] = context_.world().get_threads();
            system_profile_["native_system_timing"] = false;
            system_profile_["system_trace_scope"] =
                worker_trace_ && worker_trace_->available(context_.services()) ? "native_stages"
                                                                               : "owner_thread";
        }
    } catch (const std::exception& error) {
        system_profile_ = {{"available", false}, {"reason", error.what()}};
    }
}
void RuntimeSimulation::audio_paused(bool value) {
    if (audio_) {
        audio_->game_paused(value);
        if (!value)
            audio_->paused(false);
    }
}
void RuntimeSimulation::activate_audio(bool game_paused) {
    if (audio_) {
        audio_->game_paused(game_paused);
        audio_->paused(false);
    }
}
void RuntimeSimulation::suspend_audio() {
    if (audio_)
        audio_->paused(true);
}
void RuntimeSimulation::sync_audio() {
    if (audio_)
        audio_->synchronize(input_tick_);
}
void RuntimeSimulation::sync_animation_pose() const {
    if (!animation_)
        return;
    const auto changed = animation_->take_discontinuities();
    if (changed.empty())
        return;
    context_.evaluate_world_transforms();
    const auto nodes = context_.transform_nodes();
    for (const auto id : changed)
        if (const auto found = nodes.find(id); found != nodes.end())
            poses_.snap(id, found->second);
}
void RuntimeSimulation::reset_presentation() {
    context_.evaluate_world_transforms();
    poses_.reset(context_.transform_nodes());
    if (animation_)
        animation_->reset_presentation();
    if (particles_)
        particles_->reset();
}
Json RuntimeSimulation::presentation(double alpha, double seconds) const {
    if (!std::isfinite(seconds) || seconds < 0)
        throw std::runtime_error("Simulation presentation time must be finite and nonnegative");
    return presentation_impl(alpha, seconds);
}
Json RuntimeSimulation::presentation(double alpha) const {
    return presentation_impl(alpha, std::nullopt);
}
Json RuntimeSimulation::presentation_impl(double alpha, std::optional<double> seconds) const {
    // Owner-boundary resource adoption also proceeds while Play is paused.
    // Synchronization does not advance the runtime clock or animation time.
    if (context_.services().available(Capability::Resources))
        context_.services().resources()->synchronize();
    detail::synchronize_runtime_tile_maps(context_);
    if (physics_)
        (void)physics_->prepare_assets();
    if (animation_)
        animation_->synchronize();
    if (particles_)
        particles_->synchronize();
    sync_animation_pose();
    auto profile = context_.services().profile("runtime", "PresentationExtraction", input_tick_);
    auto result = scene_.effective_document();
    auto conditions = scene_.state_conditions();
    const auto values = poses_.evaluate(alpha);
    bool sampled = false;
    for (auto& item : result.at("entities")) {
        const auto entity = scene_.entity(item.at("id")).id();
        if (!conditions.active(entity))
            item["state_active"] = false;
        if (particles_) {
            auto state = particles_->presentation(entity, alpha);
            if (!state.is_null())
                item["particle_playback"] = std::move(state);
        }
        if (navigation_) {
            auto nav = navigation_->debug(entity);
            if (!nav.is_null())
                item["navigation_debug"] = std::move(nav);
            auto behavior = navigation_->debug_behavior(entity);
            if (!behavior.is_null())
                item["navigation_behavior_debug"] = std::move(behavior);
        }
        if (animation_) {
            const auto node = context_.world().entity(entity);
            const auto animated = animation_->presentation_properties(entity, alpha);
            if (!animated.is_null())
                merge_animation_properties(item["components"], animated, context_);
            if (node.has<Animator>())
                if (const auto reference = context_.reference(entity))
                    if (const auto status = animation_->playback(*reference))
                        item["animation_playback"] = {
                            {"state", status->state},
                            {"state_name", status->state_name},
                            {"states", status->states ? Json(status->states) : Json()},
                            {"clip", status->clip},
                            {"blend_clip", status->blend_clip ? Json(status->blend_clip) : Json()},
                            {"time", status->time},
                            {"duration", status->duration},
                            {"progress", status->progress},
                            {"speed", status->speed},
                            {"enabled", status->enabled},
                            {"loop", status->loop},
                            {"playing", status->playing},
                            {"finished", status->finished},
                            {"transitioning", status->transitioning},
                            {"transition_weight", status->transition_weight},
                            {"blend_weight", status->blend_weight}};
            auto pose = animation_->presentation(entity, alpha);
            // Disabling the animation consumer does not disable its meshes.
            // Without a live animation selection, meshes use node morph defaults
            // and the current ECS transforms, as any ordinary model instance.
            if (node.has<Animator>() && node.get<Animator>().enabled && node.has<ModelSource>())
                item["model_animation_ready"] =
                    !pose.is_null() && animation_->model_pose_ready(entity);
            if (!pose.is_null())
                item["animation_pose"] = std::move(pose);
        }
        if (seconds && !sampled) {
            const auto& components = item.at("components");
            if (components.contains("forge.mesh_renderer")) {
                const auto& renderer = components.at("forge.mesh_renderer");
                if (renderer.contains("wind") && renderer.at("wind").value("enabled", false)) {
                    result["simulation_seconds"] = *seconds;
                    sampled = true;
                }
            }
        }
        const auto found = values.find(entity);
        if (found != values.end()) {
            item["world_affine"] = found->second.affine.m;
            item["spatial_resolved"] = found->second.resolved;
        }
    }
    if (animation_)
        animation_->project_camera_shots(result, alpha);
    return result;
}
} // namespace forge
