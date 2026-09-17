#include <algorithm>
#include <cmath>
#include <forge/runtime.hpp>
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
}
void RuntimeClock::pause(Time now) {
    previous_ = now;
    accumulator_ = 0;
    paused_ = true;
}
void RuntimeClock::advance(Time now, const Tick& simulate) {
    last_ticks_ = 0;
    if (now < previous_)
        throw std::runtime_error("Runtime clock must be monotonic");
    double elapsed = std::chrono::duration<double>(now - previous_).count();
    previous_ = now;
    if (paused_)
        return;
    clamped_seconds_ += std::max(0.0, elapsed - config_.max_elapsed);
    accumulator_ += std::min(elapsed, config_.max_elapsed);
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
            {"tick", tick_},
            {"ticks_this_iteration", last_ticks_},
            {"accumulator", accumulator_},
            {"alpha", alpha()},
            {"dropped_ticks", dropped_ticks_},
            {"clamped_seconds", clamped_seconds_}};
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
void PresentationPoses::capture(const Nodes& nodes) {
    previous_ = std::move(current_);
    current_ = nodes;
    for (const auto& [id, node] : current_) {
        const auto old = previous_.find(id);
        if (old == previous_.end() || old->second.parent != node.parent ||
            old->second.parent_resolved != node.parent_resolved)
            previous_[id] = node;
    }
    std::erase_if(previous_, [&](const auto& entry) { return !current_.contains(entry.first); });
}
void PresentationPoses::snap(std::uint64_t entity) {
    if (current_.contains(entity))
        previous_[entity] = current_.at(entity);
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
RuntimeSimulation::RuntimeSimulation(WorldContext& context, Scene& scene, Module& module)
    : context_(context), scene_(scene), module_(module),
      host_{
          sizeof(ForgeHostV1), FORGE_MODULE_API_VERSION, &scene,
          [](void* p, float x, float y, float z) { static_cast<Scene*>(p)->translate(x, y, z); }} {
    if (context.role() != WorldRole::Runtime)
        throw std::runtime_error("Simulation requires a runtime WorldContext");
    auto& world = context.world();
    previous_pipeline_ = world.get_pipeline();
    gameplay_phase_ = world.entity("forge.runtime.Gameplay").add(flecs::Phase);
    transform_phase_ =
        world.entity("forge.runtime.Transforms").add(flecs::Phase).depends_on(gameplay_phase_);
    pipeline_ = world.pipeline()
                    .with(flecs::System)
                    .with<FixedSimulation>()
                    .with(flecs::Phase)
                    .cascade(flecs::DependsOn)
                    .build();
    gameplay_ = world.system("forge.runtime.NativeGameplay")
                    .kind(gameplay_phase_)
                    .immediate()
                    .run([this](flecs::iter& it) { module_.tick(host_, it.delta_time()); });
    transforms_ = world.system("forge.runtime.FinalTransforms")
                      .kind(transform_phase_)
                      .immediate()
                      .run([this](flecs::iter&) { context_.evaluate_world_transforms(); });
    world.set_pipeline(pipeline_);
    gameplay_.add<FixedSimulation>();
    transforms_.add<FixedSimulation>();
    reset_presentation();
}
RuntimeSimulation::~RuntimeSimulation() {
    context_.world().set_pipeline(previous_pipeline_);
    gameplay_.destruct();
    transforms_.destruct();
    pipeline_.destruct();
    transform_phase_.destruct();
    gameplay_phase_.destruct();
}
void RuntimeSimulation::tick(float dt) {
    if (!std::isfinite(dt) || dt <= 0)
        throw std::runtime_error("Gameplay requires a positive fixed tick delta");
    // progress updates Flecs frame/time metadata from this explicit fixed delta.
    context_.world().progress(dt);
    poses_.capture(context_.transform_nodes());
}
void RuntimeSimulation::reset_presentation() {
    context_.evaluate_world_transforms();
    poses_.reset(context_.transform_nodes());
}
Json RuntimeSimulation::presentation(double alpha) const {
    auto result = scene_.effective_document();
    const auto values = poses_.evaluate(alpha);
    for (auto& item : result.at("entities")) {
        const auto found = values.find(scene_.entity(item.at("id")).id());
        if (found != values.end()) {
            item["world_affine"] = found->second.affine.m;
            item["spatial_resolved"] = found->second.resolved;
        }
    }
    return result;
}
} // namespace forge
