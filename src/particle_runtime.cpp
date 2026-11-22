#include "particle_runtime.hpp"
#include "builtins.hpp"
#include "runtime_property_tune.hpp"
#include <algorithm>
#include <cmath>
#include <forge/application_state_activation.hpp>
#include <limits>
#include <set>
namespace forge {
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
double age(double previous, double current, double alpha) {
    require(std::isfinite(alpha) && alpha >= 0 && alpha <= 1,
            "Particle interpolation alpha must be in [0,1]");
    return std::lerp(previous, current, alpha);
}
} // namespace
ParticleRuntime::ParticleRuntime(WorldContext& world, std::filesystem::path project)
    : world_(world), project_(std::move(project)) {}
void ParticleRuntime::check() const {
    require(alive_ && owner_ == std::this_thread::get_id(),
            "Particles require their live world owner thread");
}
void ParticleRuntime::synchronize() {
    check();
    ApplicationStateActivation conditions(world_);
    // Collect native values under Flecs' iteration lock, then validate/request on
    // the owner after it returns. No exception escapes a table-locked callback.
    std::vector<std::pair<flecs::entity_t, ParticleEmitter>> sources;
    bool overflow = false;
    world_.world().each([&](flecs::entity entity, const ParticleEmitter& value) {
        if (entity.has(flecs::Prefab) || !value.enabled)
            return;
        if (sources.size() == 10000) {
            overflow = true;
            return;
        }
        sources.emplace_back(entity.id(), value);
    });
    require(!overflow, "Particle source capture exceeds the 10000-entity profile");
    std::erase_if(sources, [&](const auto& source) { return !conditions.active(source.first); });
    require(sources.size() <= particle_emitter_limit,
            "Particle scene exceeds the 64 enabled-emitter budget");
    // Worlds without emitters do not require a project/catalog (for example a
    // C++ module's isolated bootstrap). Admit asset ownership when first used.
    if (!sources.empty() && !catalog_) {
        require(!project_.empty(), "Particle Emitter requires a project asset catalog");
        catalog_ = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(project_));
    }
    std::set<flecs::entity_t> used;
    std::set<AssetId> required_effects;
    for (const auto& [entity, config] : sources) {
        validate_particle_instance_parameters(config.size_multiplier,
                                              {config.tint_r, config.tint_g, config.tint_b});
        require(config.effect.id && std::isfinite(config.playback_speed) &&
                    config.playback_speed >= 0 && config.playback_speed <= 16,
                "Particle Emitter needs a published effect and playback speed in [0,16]");
        used.insert(entity);
        required_effects.insert(config.effect.id);
    }
    for (const auto& [entity, entry] : entries_)
        if (entry.config.effect.id && !required_effects.contains(entry.config.effect.id))
            effects_.unload(entry.config.effect);
    std::erase_if(entries_, [&](const auto& entry) { return !used.contains(entry.first); });
    effects_.pump();
    std::size_t capacity = 0, scene_capacity = 0;
    for (const auto& [entity, config] : sources) {
        auto& entry = entries_[entity];
        if (!entry.ticket || entry.config.effect != config.effect ||
            entry.config.seed != config.seed) {
            entry = {};
            entry.config = config;
            entry.ticket = request_particle_effect(effects_, project_, catalog_, config.effect);
        }
        entry.config = config;
        if (!entry.effect)
            entry.effect = effects_.acquire(entry.ticket);
        if (entry.effect) {
            const auto reserved = particle_uses_scene_trails(entry.effect.get())
                                      ? particle_instance_capacity(entry.effect.get())
                                      : entry.effect->capacity;
            require(reserved <= particle_scene_capacity - capacity,
                    "Particle scene exceeds the 8192 reserved-instance budget");
            capacity += reserved;
            if (particle_uses_scene_collision(entry.effect.get())) {
                require(world_.role() == WorldRole::Runtime &&
                            world_.services().available(Capability::Physics),
                        "Scene collision particles require their Runtime World's physics module");
                require(entry.effect->capacity <=
                            particle_scene_collision_capacity - scene_capacity,
                        "Scene collision particles exceed the 1024 reserved-birth World budget");
                scene_capacity += entry.effect->capacity;
                if (particle_uses_scene_motion(entry.effect.get()))
                    world_.services().physics()->enable_motion_queries();
            }
        } else {
            const auto state = entry.ticket.inspect();
            if (resource_detail::terminal(state.state))
                throw std::runtime_error("Particle Effect load failed: " + state.diagnostic);
        }
    }
    effects_.collect();
}
bool ParticleRuntime::prepare() {
    synchronize();
    for (const auto& [id, entry] : entries_)
        if (!entry.effect)
            return false;
    return true;
}
void ParticleRuntime::tick(float dt) { tick(dt, 0); }
void ParticleRuntime::tick(float dt, std::uint64_t fixed_tick) {
    check();
    require(std::isfinite(dt) && dt > 0, "Particles need a positive fixed delta");
    synchronize();
    // Admit the entire next interval before changing any emission age.
    for (const auto& [id, entry] : entries_)
        if (entry.effect)
            require(entry.current + double(dt) * entry.config.playback_speed <= 1e9,
                    "Particle visual time exceeds the supported range");
    struct Candidate {
        ParticleSceneHistory history;
        std::optional<std::uint64_t> motion_tick;
    };
    std::map<flecs::entity_t, Candidate> candidates;
    ParticleSceneWork work;
    for (const auto& [id, entry] : entries_) {
        if (!entry.effect || !particle_uses_scene_collision(entry.effect.get()))
            continue;
        const double next_age = entry.current + double(dt) * entry.config.playback_speed;
        const double span = next_age - entry.current;
        const auto physics = world_.services().physics();
        ParticleSceneMotionQuery motion_query;
        std::optional<std::uint64_t> motion_tick;
        if (particle_uses_scene_motion(entry.effect.get()) && span > 0) {
            const auto frame = physics->motion_frame();
            // An asynchronously admitted effect may miss observation activation
            // for this step. Its first visual interval begins at the next one;
            // other emitters keep their existing progression.
            if (!frame.ready)
                continue;
            require(frame.dt == double(dt) &&
                        (!entry.motion_tick || frame.tick > *entry.motion_tick),
                    "Scene motion particles require a fresh matching adopted physics interval");
            motion_tick = frame.tick;
            motion_query = [&, frame, span](const ParticleSceneSweep& request) {
                const auto normalized = [&](double seconds) {
                    const double value = seconds / span;
                    constexpr double tolerance = 64 * std::numeric_limits<double>::epsilon();
                    require(std::isfinite(value) && value >= -tolerance && value <= 1 + tolerance,
                            "Particle motion time lies outside the adopted physics interval");
                    return std::clamp(value, 0., 1.);
                };
                const PhysicsQuerySlice slice{normalized(request.start_seconds),
                                              normalized(request.end_seconds),
                                              request.query_budget};
                const PhysicsQueryFilter filter{entry.effect->scene_collision->mask, false};
                ParticleSceneQueryResult result;
                ParticleSceneHit contact;
                if (particle_uses_scene_collision_2d(entry.effect.get())) {
                    PhysicsSweep2D sweep;
                    sweep.origin = {request.origin[0], request.origin[1]};
                    sweep.dimensions = {request.radius, 0};
                    sweep.displacement = {request.displacement[0], request.displacement[1]};
                    const auto observed = physics->shape_cast_motion_2d(sweep, slice, filter);
                    result.queries = observed.queries;
                    if (!observed.hit)
                        return result;
                    const auto& hit = observed.hit->contact;
                    static_cast<PhysicsHit&>(contact) = {
                        hit.entity,
                        {hit.position[0], hit.position[1],
                         request.origin[2] + request.displacement[2] * hit.fraction},
                        {hit.normal[0], hit.normal[1], 0},
                        hit.fraction};
                    contact.surface_velocity = {observed.hit->surface_velocity[0],
                                                observed.hit->surface_velocity[1], 0};
                } else {
                    PhysicsSweep sweep;
                    sweep.origin = {request.origin[0], request.origin[1], request.origin[2]};
                    sweep.dimensions = {request.radius, 0, 0};
                    sweep.displacement = request.displacement;
                    const auto observed = physics->shape_cast_motion(sweep, slice, filter);
                    result.queries = observed.queries;
                    if (!observed.hit)
                        return result;
                    static_cast<PhysicsHit&>(contact) = observed.hit->contact;
                    contact.surface_velocity = observed.hit->surface_velocity;
                }
                // Response uses particle age seconds, which may run at a
                // different rate from the real fixed-step interval.
                for (auto& velocity : contact.surface_velocity)
                    velocity *= frame.dt / span;
                result.hit = contact;
                return result;
            };
        }
        if (candidates.empty())
            world_.evaluate_world_transforms();
        const auto entity = world_.world().entity(id);
        require(entity.has<WorldTransform>() && entity.get<WorldTransform>().resolved,
                "Scene collision particles require a resolved birth transform");
        const PhysicsQueryFilter filter{entry.effect->scene_collision->mask, false};
        candidates.emplace(
            id, Candidate{advance_particle_scene_history(
                              entry.scene, entry.effect.get(), entry.config.seed, entry.current,
                              next_age, entity.get<WorldTransform>().affine,
                              [&](const Double3& origin, const Double3& delta, double radius) {
                                  if (particle_uses_scene_collision_2d(entry.effect.get())) {
                                      PhysicsSweep2D sweep;
                                      sweep.origin = {origin[0], origin[1]};
                                      sweep.dimensions = {radius, 0};
                                      sweep.displacement = {delta[0], delta[1]};
                                      const auto hit = physics->shape_cast_2d(sweep, filter);
                                      if (!hit)
                                          return std::optional<PhysicsHit>{};
                                      return std::optional<PhysicsHit>{
                                          PhysicsHit{hit->entity,
                                                     {hit->position[0], hit->position[1],
                                                      origin[2] + delta[2] * hit->fraction},
                                                     {hit->normal[0], hit->normal[1], 0},
                                                     hit->fraction}};
                                  }
                                  PhysicsSweep sweep;
                                  sweep.origin = {origin[0], origin[1], origin[2]};
                                  sweep.dimensions = {radius, 0, 0};
                                  sweep.displacement = delta;
                                  return physics->shape_cast(sweep, filter);
                              },
                              work, motion_query),
                          motion_tick});
    }
    std::vector<ParticleImpactRecord> impacts;
    for (auto& [id, candidate] : candidates) {
        auto& history = candidate.history;
        const auto& entry = entries_.at(id);
        if (!particle_uses_scene_impacts(entry.effect.get()))
            continue;
        const auto emitter = world_.reference(id);
        require(emitter.has_value(), "Scene impacts require a canonical emitter identity");
        const auto generation = entry.ticket.inspect().source_generation;
        for (auto& trace : history.traces) {
            const auto start =
                particle_scene_trace_start_age(entry.effect.get(), trace, entry.current);
            for (const auto& contact : trace.step.impacts) {
                require(impacts.size() < particle_scene_query_limit,
                        "Scene impacts exceed the 8192-query World budget");
                impacts.push_back({*emitter, entry.config.effect.id, entry.config.seed, generation,
                                   trace.birth.ordinal, start + contact.time, contact,
                                   particle_uses_scene_collision_2d(entry.effect.get())});
            }
            // The published broadcast owns these outcomes; presentation traces
            // retain their original motion knots without a second event copy.
            std::vector<ParticleSceneImpact>{}.swap(trace.step.impacts);
        }
    }
    // All queries and bounded candidate allocations succeeded. A failure above
    // leaves every entry's emission interval and contact history unchanged.
    for (auto& [id, entry] : entries_) {
        entry.previous = entry.current;
        const auto found = candidates.find(id);
        if (entry.effect &&
            (!particle_uses_scene_collision(entry.effect.get()) || found != candidates.end()))
            entry.current += double(dt) * entry.config.playback_speed;
        if (found != candidates.end()) {
            entry.scene = std::move(found->second.history);
            if (found->second.motion_tick)
                entry.motion_tick = found->second.motion_tick;
        }
    }
    impacts_ = std::move(impacts);
    impacts_tick_ = fixed_tick;
}
ParticleImpactFrame ParticleRuntime::impact_frame() const {
    check();
    return {impacts_tick_, impacts_.size()};
}
std::optional<ParticleImpactRecord> ParticleRuntime::impact_at(std::size_t index) const {
    check();
    if (index >= impacts_.size())
        return {};
    const auto& value = impacts_[index];
    const auto emitter = world_.resolve(value.emitter);
    if (emitter.state != WorldContext::ResolveState::Available ||
        world_.resolve(value.contact.collider).state != WorldContext::ResolveState::Available)
        return {};
    const auto selected = entries_.find(emitter.entity);
    if (selected == entries_.end() || !selected->second.effect ||
        !particle_uses_scene_impacts(selected->second.effect.get()) ||
        selected->second.ticket.inspect().source_generation != value.source_generation)
        return {};
    const auto entity = world_.world().entity(emitter.entity);
    if (!entity.has<ParticleEmitter>())
        return {};
    const auto& config = entity.get<ParticleEmitter>();
    if (!config.enabled || config.seed != value.seed || config.effect.id != value.effect)
        return {};
    return value;
}
void ParticleRuntime::reset() {
    check();
    // Reset the visual clock while retaining admitted resources. The next
    // synchronization removes obsolete native generations and cancels effects
    // no longer referenced; resetting must not discard that retirement evidence.
    for (auto& [entity, entry] : entries_) {
        entry.previous = entry.current = 0;
        entry.scene = {};
        entry.motion_tick.reset();
    }
    effects_.collect();
    impacts_.clear();
    impacts_tick_ = 0;
}
Json ParticleRuntime::presentation(flecs::entity_t entity, double alpha) const {
    check();
    const auto found = entries_.find(entity);
    if (found == entries_.end() || !found->second.effect)
        return {};
    const auto& entry = found->second;
    const auto seconds = age(entry.previous, entry.current, alpha);
    Json result{{"time", seconds},
                {"revision", entry.effect.identity().revision},
                {"generation", entry.ticket.inspect().source_generation},
                {"seed", entry.config.seed}};
    if (particle_uses_scene_collision(entry.effect.get()))
        result["world_samples"] = encode_particle_world_samples(sample_particle_scene_history(
            entry.scene, entry.effect.get(), entry.previous, entry.current, seconds));
    if (particle_uses_scene_trails(entry.effect.get()))
        result["world_paths"] = encode_particle_world_paths(sample_particle_scene_paths(
            entry.scene, entry.effect.get(), entry.previous, entry.current, seconds));
    return result;
}
std::vector<ParticleSample> ParticleRuntime::sample(flecs::entity_t entity, double alpha) const {
    check();
    const auto& entry = entries_.at(entity);
    require(bool(entry.effect), "Particle effect is not ready");
    std::vector<ParticleSample> samples;
    const auto seconds = age(entry.previous, entry.current, alpha);
    if (particle_uses_scene_collision(entry.effect.get())) {
        const auto world_samples = sample_particle_scene_history(
            entry.scene, entry.effect.get(), entry.previous, entry.current, seconds);
        samples.reserve(world_samples.size());
        for (const auto& value : world_samples)
            samples.push_back(value.sample);
    } else
        samples = sample_particle_effect(entry.effect.get(), seconds, entry.config.seed);
    modulate_particle_samples(samples, entry.config.size_multiplier,
                              {entry.config.tint_r, entry.config.tint_g, entry.config.tint_b});
    return samples;
}
Json ParticleRuntime::tune_appearance(EntityRef ref, const std::string& property,
                                      const Json& value) {
    check();
    require(world_.role() == WorldRole::Runtime && !world_.world().is_readonly(),
            "Live particle tuning requires the runtime owner");
    require(RuntimePropertyTune::particle_property(property),
            "Only particle size and RGB tint can be tuned live");
    const auto resolved = world_.resolve(ref);
    require(resolved.state == WorldContext::ResolveState::Available,
            "Live particle target is unavailable");
    auto entity = world_.world().entity(resolved.entity);
    require(!entity.has(flecs::Prefab) && entity.has<ParticleEmitter>(),
            "Live particle tuning requires an existing emitter");
    for (const auto& codec : detail::builtins()) {
        if (std::string_view(codec.name) != "forge.particle_emitter")
            continue;
        auto candidate = codec.read(entity, true);
        candidate[property] = value;
        detail::validate_components({{"forge.particle_emitter", candidate}});
        const auto typed = std::get<ParticleEmitter>(codec.decode(candidate));
        detail::validate_reflected_value(entity, typed);
        entity.set<ParticleEmitter>(typed);
        return codec.read(entity, true).at(property);
    }
    throw std::runtime_error("Particle Emitter builtin is unavailable");
}
void ParticleRuntime::shutdown() {
    check();
    entries_.clear();
    impacts_.clear();
    impacts_tick_ = 0;
    effects_.close();
    alive_ = false;
}
EngineModule particle_module(std::filesystem::path project) {
    EngineModule module;
    module.id = "forge.particles";
    module.dependencies = {"forge.core", "forge.transforms"};
    module.runtime_roles = role_mask(WorldRole::Runtime);
    module.start = [project = std::move(project)](ModuleContext& context) {
        auto runtime = std::make_shared<ParticleRuntime>(*context.owner, project);
        context.world.component<ParticleBridge>("forge.particles.private_bridge")
            .add(flecs::OnInstantiate, flecs::DontInherit);
        context.world.set<ParticleBridge>({runtime});
        context.state = std::move(runtime);
    };
    module.stop = [](ModuleContext& context) {
        if (const auto runtime = std::static_pointer_cast<ParticleRuntime>(context.state))
            runtime->shutdown();
    };
    return module;
}
std::shared_ptr<ParticleRuntime> particle_runtime(WorldContext& world) {
    return std::dynamic_pointer_cast<ParticleRuntime>(particle_runtime_access(world));
}
} // namespace forge
