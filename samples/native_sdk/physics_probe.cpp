// Exact-SDK physics boundary proof; no Jolt linkage or headers.
#include <cstdio>
#include <flecs.h>
#include <forge/character_components.hpp>
#include <forge/native_sdk.h>
#include <forge/native_sdk_identity.h>
#include <forge/physics_components.hpp>
#include <forge/transform_components.hpp>
#include <stdexcept>
#include <thread>
namespace {
int32_t FORGE_SDK_CALL schemas(const ForgeSdkWorldV1* host, char*, uint32_t) {
    flecs::world w(host->world);
    w.component<forge::CharacterController>("forge.character_controller");
    w.component<forge::LocalTranslation>("forge.local_translation");
    w.component<forge::PhysicsBody>("forge.physics_body");
    w.component<forge::BoxCollider>("forge.box_collider");
    return 1;
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    if (!(host->capabilities & FORGE_SDK_PHYSICS)) {
        std::snprintf(error, capacity, "Missing physics capability");
        return 0;
    }
    flecs::world w(host->world);
    w.system<const forge::PhysicsBody>()
        .kind(host->fixed_phase)
        .each([](flecs::entity e, const forge::PhysicsBody& old) {
            if (old.motion != 2)
                return;
            auto next = old;
            next.friction = .75f;
            e.set<forge::PhysicsBody>(next);
            e.remove<forge::BoxCollider>();
            e.set<forge::BoxCollider>({1, 1, 1});
        })
        .add(host->fixed_tag);
    w.system<const forge::PhysicsBody>()
        .kind(host->post_physics_phase)
        .each([host](flecs::iter& it, size_t, const forge::PhysicsBody& b) {
            if (b.motion != 2)
                return;
            if (it.delta_time() < .016f || it.delta_time() > .017f)
                throw std::runtime_error("SDK fixed dt changed");
            ForgeSdkActionV1 action{};
            action.size = sizeof(action);
            host->read_action(host->context, "12345678-1234-4234-8234-123456789abc", &action);
            ForgeSdkPhysicsHitV1 hit{};
            hit.size = sizeof(hit);
            const double origin[] = {0, 10, 0}, delta[] = {0, -20, 0};
            if (host->raycast(host->context, origin, delta, &hit) != 1)
                throw std::runtime_error("SDK raycast failed");
            if (!hit.entity[0] || !hit.scene[0])
                throw std::runtime_error("SDK raycast identity absent");
            host->diagnostic(host->context, 1, "Physics SDK raycast succeeded");
        })
        .add(host->fixed_tag);
    w.system<const forge::CharacterController, const forge::LocalTranslation>()
        .kind(host->post_physics_phase)
        .each([host](const forge::CharacterController&, const forge::LocalTranslation& position) {
            const double origin[] = {position.x, position.y + 10, position.z},
                         delta[] = {0, -20, 0};
            ForgeSdkPhysicsHitV1 hit{};
            hit.size = sizeof(hit);
            if (host->raycast_filtered(host->context, origin, delta, UINT32_MAX, 0, &hit) != 1)
                throw std::runtime_error("SDK character ray failed");
            ForgeSdkCharacterV1 state{};
            state.size = sizeof(state);
            if (host->character_state(host->context, hit.scene, hit.entity, &state) != 1)
                throw std::runtime_error("SDK character state failed");
            const double velocity[] = {1, 0, 0}, invalid[] = {0, 1, 0};
            if (host->character_command(host->context, hit.scene, hit.entity, 0, invalid, nullptr,
                                        0, 0) != 0)
                throw std::runtime_error("SDK accepted nonplanar movement");
            if (host->character_command(host->context, hit.scene, hit.entity, 0, velocity, nullptr,
                                        0, 0) != 1)
                throw std::runtime_error("SDK character movement failed");
            ForgeSdkSweepV1 sweep{};
            sweep.size = sizeof(sweep);
            sweep.shape = 1;
            sweep.layer_mask = UINT32_MAX;
            sweep.dimensions[0] = .1;
            sweep.rotation[3] = 1;
            for (unsigned i = 0; i < 3; ++i) {
                sweep.origin[i] = origin[i];
                sweep.displacement[i] = delta[i];
            }
            if (host->shape_cast(host->context, &sweep, &hit) != 1)
                throw std::runtime_error("SDK shape query failed");
            sweep.layer_mask = 0;
            if (host->shape_cast(host->context, &sweep, &hit) != 0 || hit.entity[0])
                throw std::runtime_error("SDK shape miss retained output");
            bool rejected = false;
            std::thread worker([&] {
                ForgeSdkCharacterV1 foreign{};
                foreign.size = sizeof(foreign);
                rejected =
                    host->character_state(host->context, "invalid", "invalid", &foreign) == 0 &&
                    host->shape_cast(host->context, &sweep, &hit) == -1;
            });
            worker.join();
            if (!rejected)
                throw std::runtime_error("SDK character/query accepted foreign thread");
            host->diagnostic(
                host->context, 1,
                "Character SDK copied state, movement, filtered sweep and thread guards succeeded");
        })
        .add(host->fixed_tag);
    return 1;
}
const char* dependencies[] = {"forge.physics", "forge.input"};
const ForgeNativeSdkV1 api = {sizeof(ForgeNativeSdkV1),
                              FORGE_NATIVE_SDK_ABI,
                              FORGE_NATIVE_SDK_FINGERPRINT,
                              "project.physics_probe",
                              "1",
                              dependencies,
                              2,
                              FORGE_SDK_RUNTIME,
                              FORGE_SDK_RUNTIME,
                              FORGE_SDK_PHYSICS,
                              FORGE_SDK_PHYSICS | FORGE_SDK_DIAGNOSTICS,
                              &ecs_init,
                              &ecs_os_api,
                              schemas,
                              start,
                              nullptr};
} // namespace
extern "C" FORGE_SDK_EXPORT const ForgeNativeSdkV1* FORGE_SDK_CALL forge_native_sdk_v1() {
    return &api;
}
