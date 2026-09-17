// Exact-version engine component access; no Ozz types or broad animation service.
#include <flecs.h>
#include <forge/animation_components.hpp>
#include <forge/native_sdk.h>
#include <forge/native_sdk_identity.h>
namespace {
int32_t FORGE_SDK_CALL schemas(const ForgeSdkWorldV1* h, char*, uint32_t) {
    flecs::world w(h->world);
    w.component<forge::Animator>("forge.animator");
    return 1;
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* h, char*, uint32_t) {
    flecs::world w(h->world);
    w.system<const forge::Animator>()
        .kind(h->fixed_phase)
        .each([](flecs::entity e, const forge::Animator& previous) {
            auto next = previous;
            next.playback_speed = 2;
            e.set(next);
        })
        .add(h->fixed_tag);
    return 1;
}
const char* dependencies[] = {"forge.animation", "forge.input"};
const ForgeNativeSdkV1 api = {sizeof(ForgeNativeSdkV1),
                              FORGE_NATIVE_SDK_ABI,
                              FORGE_NATIVE_SDK_FINGERPRINT,
                              "project.animation_probe",
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
