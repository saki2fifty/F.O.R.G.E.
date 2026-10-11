#include "forge.registration.hpp"
#include <cstdio>
#include <exception>
#include <flecs.h>
#include <forge/native_sdk_identity.h>
#include <stdexcept>
namespace {
int32_t FORGE_SDK_CALL schema(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    return forge_register_components(host, error, capacity);
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    try {
        if (!(host->capabilities & FORGE_SDK_PHYSICS))
            throw std::runtime_error("Physics capability unavailable");
        forge_register_systems(host);
        return 1;
    } catch (const std::exception& e) {
        if (capacity)
            std::snprintf(error, capacity, "%s", e.what());
        return 0;
    } catch (...) {
        if (capacity)
            std::snprintf(error, capacity, "%s", "Body Loads startup failed");
        return 0;
    }
}
void FORGE_SDK_CALL stop(const ForgeSdkWorldV1*) {}
const char* dependencies[] = {"forge.physics"};
const ForgeNativeSdkV1 descriptor = {sizeof(ForgeNativeSdkV1),
                                     FORGE_NATIVE_SDK_ABI,
                                     FORGE_NATIVE_SDK_FINGERPRINT,
                                     "project.gameplay",
                                     "1",
                                     dependencies,
                                     1,
                                     FORGE_SDK_AUTHORING | FORGE_SDK_RUNTIME | FORGE_SDK_VALIDATION,
                                     FORGE_SDK_RUNTIME,
                                     FORGE_SDK_PHYSICS,
                                     FORGE_SDK_PHYSICS,
                                     &ecs_init,
                                     &ecs_os_api,
                                     schema,
                                     start,
                                     stop,
                                     nullptr,
                                     nullptr,
                                     nullptr};
} // namespace
extern "C" FORGE_SDK_EXPORT const ForgeNativeSdkV1* FORGE_SDK_CALL forge_native_sdk_v1() {
    return &descriptor;
}
