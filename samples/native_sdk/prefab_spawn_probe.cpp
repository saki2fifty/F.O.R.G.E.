// Exact-SDK integration driver for the original optional Gallery64 C++ example.
#include <cstdio>
#include <flecs.h>
#include <forge/native_sdk_identity.h>
#include <forge/sdk_client.hpp>
#include <stdexcept>
void forge_register_system_SpawnPrefabs(const ForgeSdkWorldV1*);
namespace {
int32_t FORGE_SDK_CALL schema(const ForgeSdkWorldV1*, char*, uint32_t) { return 1; }
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    try {
        forge_register_system_SpawnPrefabs(host);
        return 1;
    } catch (const std::exception& e) {
        if (capacity)
            std::snprintf(error, capacity, "%s", e.what());
    } catch (...) {
        if (capacity)
            std::snprintf(error, capacity, "Prefab example failed to start");
    }
    return 0;
}
const char* dependencies[] = {"forge.transforms"};
const ForgeNativeSdkV1 api{sizeof(ForgeNativeSdkV1),
                           FORGE_NATIVE_SDK_ABI,
                           FORGE_NATIVE_SDK_FINGERPRINT,
                           "project.prefab_spawn_probe",
                           "1",
                           dependencies,
                           1,
                           FORGE_SDK_AUTHORING | FORGE_SDK_RUNTIME | FORGE_SDK_VALIDATION,
                           FORGE_SDK_RUNTIME,
                           0,
                           FORGE_SDK_DIAGNOSTICS,
                           &ecs_init,
                           &ecs_os_api,
                           schema,
                           start,
                           nullptr};
} // namespace
extern "C" FORGE_SDK_EXPORT const ForgeNativeSdkV1* FORGE_SDK_CALL forge_native_sdk_v1() {
    return &api;
}
