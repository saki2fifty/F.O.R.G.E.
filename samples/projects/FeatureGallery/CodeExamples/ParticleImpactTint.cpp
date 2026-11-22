// Add this System to a matching-SDK project. It changes runtime emitter tint;
// the Scene and particle source remain authored separately.
#include <flecs.h>
#include <forge/render_components.hpp>
#include <forge/sdk_client.hpp>
#include <string>
#include <unordered_set>

void forge_register_system_ParticleImpactTint(const ForgeSdkWorldV1* host) {
    flecs::world(host->world)
        .system<forge::ParticleEmitter>()
        .kind(host->fixed_phase)
        .run([host](flecs::iter& it) {
            const forge::sdk::Client client(host);
            const auto count = client.particle_impact_count();
            if (count <= 0 || count > int32_t(FORGE_SDK_PARTICLE_IMPACT_LIMIT))
                return;
            // Transient per-update membership; no persistent registry and no
            // consumption of observations belonging to other systems.
            std::unordered_set<std::string> impacted;
            for (int32_t index = 0; index < count; ++index) {
                ForgeSdkParticleImpactV1 event{};
                if (client.particle_impact_event(index, event) == 1)
                    impacted.insert(std::string(event.emitter_scene) + event.emitter_entity);
            }
            while (it.next()) {
                auto emitters = it.field<forge::ParticleEmitter>(0);
                for (auto row : it) {
                    ForgeSdkEntityRefV1 actor{};
                    if (client.entity_reference(it.entity(row).id(), actor) != 1 ||
                        !impacted.contains(std::string(actor.scene_uuid) + actor.entity_uuid))
                        continue;
                    emitters[row].tint_r = 1;
                    emitters[row].tint_g = .15f;
                    emitters[row].tint_b = .5f;
                }
            }
        })
        .add(host->fixed_tag);
}
