// Exact SDK proof: engine-owned components and commands, no miniaudio types.
#include <cstdio>
#include <flecs.h>
#include <forge/audio_components.hpp>
#include <forge/native_sdk.h>
#include <forge/native_sdk_identity.h>
namespace {
int32_t FORGE_SDK_CALL schemas(const ForgeSdkWorldV1* h, char*, uint32_t) {
    flecs::world w(h->world);
    w.component<forge::AudioSource>("forge.audio_source");
    return 1;
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* h, char* error, uint32_t size) {
    if (!(h->capabilities & FORGE_SDK_AUDIO)) {
        std::snprintf(error, size, "Missing Audio capability");
        return 0;
    }
    flecs::world w(h->world);
    w.system<const forge::AudioSource>()
        .kind(h->fixed_phase)
        .each([h](flecs::entity e, const forge::AudioSource& old) {
            auto next = old;
            next.gain = .5f;
            e.set(next);
            const auto tick = ecs_get_world_info(h->world)->frame_count_total + 1;
            const auto result =
                h->audio_source(h->context, "11111111-1111-4111-8111-111111111111",
                                "22222222-2222-4222-8222-222222222222", tick % 2 ? 1u : 0u);
            h->diagnostic(h->context, result ? 1u : 3u,
                          result ? "Audio SDK command queued" : "Audio SDK command rejected");
        })
        .add(h->fixed_tag);
    return 1;
}
const char* dependencies[] = {"forge.audio", "forge.input"};
const ForgeNativeSdkV1 api = {sizeof(ForgeNativeSdkV1),
                              FORGE_NATIVE_SDK_ABI,
                              FORGE_NATIVE_SDK_FINGERPRINT,
                              "project.audio_probe",
                              "1",
                              dependencies,
                              2,
                              FORGE_SDK_RUNTIME,
                              FORGE_SDK_RUNTIME,
                              FORGE_SDK_AUDIO,
                              FORGE_SDK_AUDIO | FORGE_SDK_DIAGNOSTICS,
                              &ecs_init,
                              &ecs_os_api,
                              schemas,
                              start,
                              nullptr};
} // namespace
extern "C" FORGE_SDK_EXPORT const ForgeNativeSdkV1* FORGE_SDK_CALL forge_native_sdk_v1() {
    return &api;
}
