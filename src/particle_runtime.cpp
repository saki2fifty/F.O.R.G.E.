#include "particle_runtime.hpp"
#include "builtins.hpp"
#include "runtime_property_tune.hpp"
#include <cmath>
#include <set>
namespace forge {
namespace {
struct ParticleBridge {
    std::weak_ptr<ParticleRuntime> runtime;
};
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
    // Collect native values under Flecs' iteration lock, then validate/request on
    // the owner after it returns. No exception escapes a table-locked callback.
    std::vector<std::pair<flecs::entity_t, ParticleEmitter>> sources;
    bool overflow = false;
    world_.world().each([&](flecs::entity entity, const ParticleEmitter& value) {
        if (entity.has(flecs::Prefab) || !value.enabled)
            return;
        if (sources.size() == particle_emitter_limit) {
            overflow = true;
            return;
        }
        sources.emplace_back(entity.id(), value);
    });
    require(!overflow, "Particle scene exceeds the 64 enabled-emitter budget");
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
    std::size_t capacity = 0;
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
            require(entry.effect->capacity <= particle_scene_capacity - capacity,
                    "Particle scene exceeds the 8192 reserved-instance budget");
            capacity += entry.effect->capacity;
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
void ParticleRuntime::tick(float dt) {
    check();
    require(std::isfinite(dt) && dt > 0, "Particles need a positive fixed delta");
    synchronize();
    // Admit the entire next interval before changing any emission age.
    for (const auto& [id, entry] : entries_)
        if (entry.effect)
            require(entry.current + double(dt) * entry.config.playback_speed <= 1e9,
                    "Particle visual time exceeds the supported range");
    for (auto& [id, entry] : entries_) {
        entry.previous = entry.current;
        if (entry.effect)
            entry.current += double(dt) * entry.config.playback_speed;
    }
}
void ParticleRuntime::reset() {
    check();
    // Reset the visual clock while retaining admitted resources. The next
    // synchronization removes obsolete native generations and cancels effects
    // no longer referenced; resetting must not discard that retirement evidence.
    for (auto& [entity, entry] : entries_)
        entry.previous = entry.current = 0;
    effects_.collect();
}
Json ParticleRuntime::presentation(flecs::entity_t entity, double alpha) const {
    check();
    const auto found = entries_.find(entity);
    if (found == entries_.end() || !found->second.effect)
        return {};
    const auto& entry = found->second;
    return {{"time", age(entry.previous, entry.current, alpha)},
            {"revision", entry.effect.identity().revision},
            {"generation", entry.ticket.inspect().source_generation},
            {"seed", entry.config.seed}};
}
std::vector<ParticleSample> ParticleRuntime::sample(flecs::entity_t entity, double alpha) const {
    check();
    const auto& entry = entries_.at(entity);
    require(bool(entry.effect), "Particle effect is not ready");
    auto samples = sample_particle_effect(
        entry.effect.get(), age(entry.previous, entry.current, alpha), entry.config.seed);
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
    return world.world().has<ParticleBridge>() ? world.world().get<ParticleBridge>().runtime.lock()
                                               : nullptr;
}
} // namespace forge
