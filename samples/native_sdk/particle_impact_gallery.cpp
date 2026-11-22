// Matching public-SDK descriptor for the editable Gallery System.
#include <flecs.h>
#include <forge/native_sdk_identity.h>
#include <forge/render_components.hpp>
#include <forge/sdk_client.hpp>

void forge_register_system_ParticleImpactTint(const ForgeSdkWorldV1*);
namespace {
int32_t FORGE_SDK_CALL schemas(const ForgeSdkWorldV1* host, char*, uint32_t) {
    flecs::world(host->world).component<forge::ParticleEmitter>("forge.particle_emitter");
    return 1;
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char*, uint32_t) {
    forge_register_system_ParticleImpactTint(host);
    return 1;
}
const char* dependencies[] = {"forge.physics", "forge.particles"};
const ForgeNativeSdkV1 api = {sizeof(ForgeNativeSdkV1),
                              FORGE_NATIVE_SDK_ABI,
                              FORGE_NATIVE_SDK_FINGERPRINT,
                              "project.particle_impact_gallery",
                              "1",
                              dependencies,
                              2,
                              FORGE_SDK_RUNTIME,
                              FORGE_SDK_RUNTIME,
                              FORGE_SDK_PHYSICS,
                              FORGE_SDK_PHYSICS,
                              &ecs_init,
                              &ecs_os_api,
                              schemas,
                              start,
                              nullptr};
} // namespace
extern "C" FORGE_SDK_EXPORT const ForgeNativeSdkV1* FORGE_SDK_CALL forge_native_sdk_v1() {
    return &api;
}
