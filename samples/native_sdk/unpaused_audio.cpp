// Original public SDK example for Gallery84's independent music/UI sources.
#include <cstdio>
#include <flecs.h>
#include <forge/audio_components.hpp>
#include <forge/native_sdk_identity.h>
#include <forge/sdk_client.hpp>
#include <stdexcept>
namespace {
constexpr const char* scene = "15100000-1510-4151-8151-151000000000";
constexpr const char* ui_source = "15100000-1510-4151-8151-151000000002";
constexpr const char* gameplay_source = "15100000-1510-4151-8151-151000000003";
int32_t FORGE_SDK_CALL schemas(const ForgeSdkWorldV1* host, char*, uint32_t) {
    flecs::world(host->world).component<forge::AudioSource>("forge.audio_source");
    return 1;
}
int32_t FORGE_SDK_CALL controls(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    try {
        const forge::sdk::Client client(host);
        const auto audio = client.query(forge::sdk::Capability::Audio);
        if (!audio.available || !audio.callable || (audio.flags & FORGE_SDK_FIXED_ONLY))
            throw std::runtime_error("Audio control capability is unavailable");
        ForgeSdkActionV1 pulse{}, stop{}, denied{};
        client.control("15100000-1510-4151-8151-1510000000b1", pulse);
        client.control("15100000-1510-4151-8151-1510000000b2", stop);
        client.control("15100000-1510-4151-8151-1510000000b3", denied);
        if (pulse.pressed || stop.pressed) {
            const auto operation = stop.pressed ? FORGE_SDK_AUDIO_STOP : FORGE_SDK_AUDIO_ONE_SHOT;
            if (!client.audio_operation(scene, ui_source, operation))
                throw std::runtime_error("Independent UI audio request rejected");
            host->diagnostic(host->context, 1,
                             stop.pressed ? "Independent UI stopped"
                                          : "Independent UI one-shot queued");
        }
        if (denied.pressed) {
            if (client.audio_operation(scene, gameplay_source, FORGE_SDK_AUDIO_RESTART))
                throw std::runtime_error("Gameplay-only source accepted a control-domain command");
            host->diagnostic(host->context, 1, "Gameplay-only control rejected as expected");
        }
        return 1;
    } catch (const std::exception& failure) {
        if (capacity)
            std::snprintf(error, capacity, "%s", failure.what());
        return 0;
    }
}
const char* dependencies[]{"forge.audio", "forge.input"};
const ForgeNativeSdkV1 api{sizeof(ForgeNativeSdkV1),
                           FORGE_NATIVE_SDK_ABI,
                           FORGE_NATIVE_SDK_FINGERPRINT,
                           "project.unpaused_audio",
                           "1",
                           dependencies,
                           2,
                           FORGE_SDK_RUNTIME | FORGE_SDK_VALIDATION,
                           FORGE_SDK_RUNTIME,
                           FORGE_SDK_AUDIO,
                           FORGE_SDK_AUDIO | FORGE_SDK_DIAGNOSTICS,
                           &ecs_init,
                           &ecs_os_api,
                           schemas,
                           nullptr,
                           nullptr,
                           controls};
} // namespace
extern "C" FORGE_SDK_EXPORT const ForgeNativeSdkV1* FORGE_SDK_CALL forge_native_sdk_v1() {
    return &api;
}
