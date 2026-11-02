#include <cstdio>
#include <flecs.h>
#include <forge/application_state_components.hpp>
#include <forge/native_sdk_identity.h>
#include <forge/sdk_client.hpp>
#include <memory>

namespace {
int32_t FORGE_SDK_CALL schemas(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    try {
        flecs::world(host->world)
            .component<forge::ApplicationStateGate>("forge.application_state_gate");
        return 1;
    } catch (const std::exception& exception) {
        if (capacity)
            std::snprintf(error, capacity, "%s", exception.what());
        return 0;
    }
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    try {
        struct Observed {
            bool playing = false, menu = false;
        };
        auto observed = std::make_shared<Observed>();
        flecs::world world(host->world);
        world.system<const forge::ApplicationStateGate>()
            .kind(host->fixed_phase)
            .each([host, observed](const forge::ApplicationStateGate& value) {
                // Ordinary project queries remain explicit. They still see an
                // inactive object's component; only stock consumers apply the gate.
                if (value.enabled && value.domain == "app") {
                    if (value.state == "playing" && !observed->playing) {
                        host->diagnostic(host->context, 1,
                                         "SDK State Condition playing value verified");
                        observed->playing = true;
                    }
                    if (value.state == "menu" && !observed->menu) {
                        host->diagnostic(host->context, 1,
                                         "SDK State Condition inactive value verified");
                        observed->menu = true;
                    }
                }
            })
            .add(host->fixed_tag);
        return 1;
    } catch (const std::exception& exception) {
        if (capacity)
            std::snprintf(error, capacity, "%s", exception.what());
        return 0;
    }
}
const ForgeNativeSdkV1 api{sizeof(api),
                           FORGE_NATIVE_SDK_ABI,
                           FORGE_NATIVE_SDK_FINGERPRINT,
                           "project.state_condition_probe",
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
