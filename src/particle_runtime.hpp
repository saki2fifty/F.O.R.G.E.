#pragma once
#include "particle_effect_resource.hpp"
#include <forge/render_components.hpp>
#include <forge/world.hpp>
namespace forge {
// World-owned visual emission age. The existing simulation supplies fixed dt;
// presentation reads interpolate compact state and never progress another clock.
class ParticleRuntime {
  public:
    ParticleRuntime(WorldContext&, std::filesystem::path);
    bool prepare();
    void synchronize();
    void tick(float);
    void reset();
    Json presentation(flecs::entity_t, double) const;
    std::vector<ParticleSample> sample(flecs::entity_t, double) const;
    Json tune_appearance(EntityRef, const std::string& property, const Json& value);
    void shutdown();

  private:
    struct Entry {
        ParticleEmitter config;
        ResourceTicket ticket;
        ResourceLease<ParticleEffectAsset> effect;
        double previous = 0, current = 0;
    };
    void check() const;
    WorldContext& world_;
    std::filesystem::path project_;
    const std::thread::id owner_ = std::this_thread::get_id();
    std::shared_ptr<const AssetCatalog> catalog_;
    ResourcePool<ParticleEffectAsset> effects_{{1, 64, 256, 128 * 1024}};
    std::map<flecs::entity_t, Entry> entries_;
    bool alive_ = true;
};
EngineModule particle_module(std::filesystem::path);
std::shared_ptr<ParticleRuntime> particle_runtime(WorldContext&);
} // namespace forge
