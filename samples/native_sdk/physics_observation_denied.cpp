// Execute valid fixed-tick reads without a Physics capability grant.
#include <cstdio>
#include <flecs.h>
#include <forge/native_sdk.h>
#include <forge/native_sdk_identity.h>
#include <forge/physics_components.hpp>
#include <stdexcept>
namespace {
int32_t FORGE_SDK_CALL schemas(const ForgeSdkWorldV1* host, char*, uint32_t) {
    flecs::world(host->world).component<forge::PhysicsBody>("forge.physics_body");
    return 1;
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char*, uint32_t) {
    if (host->capabilities & FORGE_SDK_PHYSICS)
        throw std::runtime_error("Denied observation fixture accidentally has Physics access");
    flecs::world world(host->world);
    world.system<const forge::PhysicsBody>()
        .kind(host->post_physics_phase)
        .each([host](flecs::entity entity, const forge::PhysicsBody&) {
            ForgeSdkEntityRefV1 ref{};
            ref.size = sizeof(ref);
            if (host->entity_reference(host->context, entity.id(), &ref) != 1)
                throw std::runtime_error("Denied fixture has no valid target identity");
            ForgeSdkPhysicsBodyStateV1 state{};
            state.size = sizeof(state);
            state.active = 1;
            state.linear_velocity[0] = 42;
            const double point[] = {0, 5, 0};
            double velocity[] = {42, 42, 42};
            if (host->physics_body_state(host->context, ref.scene_uuid, ref.entity_uuid, &state) !=
                    -1 ||
                state.active || state.linear_velocity[0] ||
                host->physics_point_velocity(host->context, ref.scene_uuid, ref.entity_uuid, point,
                                             velocity) != -1 ||
                velocity[0] || velocity[1] || velocity[2])
                throw std::runtime_error("SDK exposed solver state without Physics capability");
            for (uint32_t channel :
                 {FORGE_SDK_PHYSICS_LINEAR_VELOCITY, FORGE_SDK_PHYSICS_ANGULAR_VELOCITY})
                if (host->physics_set_velocity(host->context, ref.scene_uuid, ref.entity_uuid,
                                               channel, point) != 0)
                    throw std::runtime_error("SDK accepted velocity without Physics capability");
            ForgeSdkPhysicsContactV1 contact{};
            contact.size = sizeof(contact);
            contact.begin = 42;
            contact.tick = 42;
            contact.first_scene_uuid[0] = 'x';
            if (host->physics_contact_count(host->context) != -1 ||
                host->physics_contact_event(host->context, 0, &contact) != -1 || contact.begin ||
                contact.tick || contact.first_scene_uuid[0])
                throw std::runtime_error("SDK exposed contacts without Physics capability");
            if (host->diagnostic(host->context, 1,
                                 "Body observation denied: Physics capability absent") != 1)
                throw std::runtime_error("Denied observation fixture marker failed");
        })
        .add(host->fixed_tag);
    return 1;
}
const char* dependencies[] = {"forge.physics", "forge.input"};
const ForgeNativeSdkV1 api = {sizeof(ForgeNativeSdkV1),
                              FORGE_NATIVE_SDK_ABI,
                              FORGE_NATIVE_SDK_FINGERPRINT,
                              "project.physics_observation_denied",
                              "1",
                              dependencies,
                              2,
                              FORGE_SDK_RUNTIME,
                              FORGE_SDK_RUNTIME,
                              0,
                              FORGE_SDK_DIAGNOSTICS,
                              &ecs_init,
                              &ecs_os_api,
                              schemas,
                              start,
                              nullptr,
                              nullptr,
                              nullptr,
                              nullptr};
} // namespace
extern "C" FORGE_SDK_EXPORT const ForgeNativeSdkV1* FORGE_SDK_CALL forge_native_sdk_v1() {
    return &api;
}
