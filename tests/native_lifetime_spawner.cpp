// Test fixture: ordinary SDK creation, observed through the actual Play snapshot.
#include <flecs.h>
#include <forge/native_sdk_identity.h>
#include <forge/physics_components.hpp>
#include <forge/sdk_client.hpp>
#include <forge/transform_components.hpp>
namespace {
struct Spawn {
    std::uint64_t request = 0;
    bool complete = false;
};
int32_t FORGE_SDK_CALL schemas(const ForgeSdkWorldV1* host, char*, uint32_t) {
    flecs::world world(host->world);
    world.component<Spawn>();
    (void)world.id<forge::LocalTranslation>();
    (void)world.id<forge::PhysicsBody2D>();
    (void)world.id<forge::BoxCollider2D>();
    return 1;
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char*, uint32_t) {
    flecs::world world(host->world);
    world.entity().set<Spawn>({});
    world.system<Spawn>()
        .kind(host->fixed_phase)
        .each([host](Spawn& spawn) {
            if (spawn.complete)
                return;
            forge::sdk::Client client(host);
            if (!spawn.request) {
                spawn.request = client.request_entity("MCP scoped runtime root");
                if (!spawn.request)
                    host->diagnostic(host->context, 3, "Lifetime fixture creation failed");
                return;
            }
            ForgeSdkEntityV1 result{};
            if (client.inspect_entity(spawn.request, result) &&
                result.state == FORGE_SDK_ENTITY_READY) {
                flecs::world current(host->world);
                current.entity(result.native_entity)
                    .set<forge::LocalTranslation>({})
                    .set<forge::PhysicsBody2D>({})
                    .set<forge::BoxCollider2D>({});
                client.release_entity(spawn.request);
                spawn.complete = true;
            }
        })
        .add(host->fixed_tag);
    return 1;
}
const ForgeNativeSdkV1 api{sizeof(api),
                           FORGE_NATIVE_SDK_ABI,
                           FORGE_NATIVE_SDK_FINGERPRINT,
                           "project.runtime_lifetime_probe",
                           "1",
                           nullptr,
                           0,
                           FORGE_SDK_RUNTIME,
                           FORGE_SDK_RUNTIME,
                           0,
                           FORGE_SDK_DIAGNOSTICS,
                           &ecs_init,
                           &ecs_os_api,
                           schemas,
                           start,
                           nullptr};
} // namespace
extern "C" FORGE_SDK_EXPORT const ForgeNativeSdkV1* FORGE_SDK_CALL forge_native_sdk_v1() {
    return &api;
}
