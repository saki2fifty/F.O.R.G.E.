// Matching SDK proof: gameplay changes the engine-owned ParticleEmitter value.
#include <flecs.h>
#include <forge/native_sdk.h>
#include <forge/native_sdk_identity.h>
#include <forge/render_components.hpp>
namespace {
int32_t FORGE_SDK_CALL schemas(const ForgeSdkWorldV1* host, char*, uint32_t) {
    flecs::world world(host->world);
    world.component<forge::ParticleEmitter>("forge.particle_emitter");
    return 1;
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char*, uint32_t) {
    flecs::world world(host->world);
    world.system<forge::ParticleEmitter>()
        .kind(host->fixed_phase)
        .each([](forge::ParticleEmitter& value) {
            value.seed = 9;
            value.playback_speed = 2.;
            value.size_multiplier = 2.;
            value.tint_r = .25f;
            value.tint_g = .5f;
            value.tint_b = .75f;
        })
        .add(host->fixed_tag);
    return 1;
}
const char* dependencies[] = {"forge.transforms", "forge.particles"};
const ForgeNativeSdkV1 api = {sizeof(ForgeNativeSdkV1),
                              FORGE_NATIVE_SDK_ABI,
                              FORGE_NATIVE_SDK_FINGERPRINT,
                              "project.particle_probe",
                              "1",
                              dependencies,
                              2,
                              FORGE_SDK_RUNTIME,
                              FORGE_SDK_RUNTIME,
                              0,
                              0,
                              &ecs_init,
                              &ecs_os_api,
                              schemas,
                              start,
                              nullptr};
} // namespace
extern "C" FORGE_SDK_EXPORT const ForgeNativeSdkV1* FORGE_SDK_CALL forge_native_sdk_v1() {
    return &api;
}
