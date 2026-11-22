#pragma once
#include "particle_scene_impact.hpp"
#include <forge/world.hpp>
#include <memory>
#include <optional>

namespace forge {
struct ParticleImpactRecord {
    EntityRef emitter;
    AssetId effect;
    std::uint32_t seed = 0;
    std::uint64_t source_generation = 0, birth = 0;
    double age = 0;
    ParticleSceneImpact contact;
    bool planar_2d = false;
    bool operator==(const ParticleImpactRecord&) const = default;
};
struct ParticleImpactFrame {
    std::uint64_t tick = 0;
    std::size_t count = 0;
    bool operator==(const ParticleImpactFrame&) const = default;
};
// The existing weak World bridge exposes copied observations without linking
// core SDK dispatch back to the concrete particle library. Owns no runtime.
class ParticleRuntimeAccess {
  public:
    virtual ~ParticleRuntimeAccess() = default;
    virtual ParticleImpactFrame impact_frame() const = 0;
    virtual std::optional<ParticleImpactRecord> impact_at(std::size_t) const = 0;
};
struct ParticleBridge {
    std::weak_ptr<ParticleRuntimeAccess> runtime;
};
inline std::shared_ptr<ParticleRuntimeAccess> particle_runtime_access(WorldContext& world) {
    // Discovery must not register a private component inside an active pipeline.
    if (!world.world().lookup("forge.particles.private_bridge"))
        return {};
    return world.world().has<ParticleBridge>() ? world.world().get<ParticleBridge>().runtime.lock()
                                               : nullptr;
}
} // namespace forge
