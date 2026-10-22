#include "forge.registration.hpp"
#include <forge/native_sdk_identity.h>
#include <stdexcept>
namespace {
int32_t FORGE_SDK_CALL schema(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    return forge_register_components(host, error, capacity);
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    try {
        if (!(host->capabilities & FORGE_SDK_PHYSICS))
            throw std::runtime_error("Character Movement 2D needs Physics capability");
        forge_register_systems(host);
        return 1;
    } catch (const std::exception& failure) {
        if (capacity)
            std::snprintf(error, capacity, "%s", failure.what());
        return 0;
    } catch (...) {
        if (capacity)
            std::snprintf(error, capacity, "%s", "Character Movement 2D startup failed");
        return 0;
    }
}
const char* dependencies[]{"forge.physics", "forge.input"};
const ForgeNativeSdkV1 descriptor{sizeof(ForgeNativeSdkV1),
                                  FORGE_NATIVE_SDK_ABI,
                                  FORGE_NATIVE_SDK_FINGERPRINT,
                                  "project.gameplay",
                                  "1",
                                  dependencies,
                                  2,
                                  FORGE_SDK_AUTHORING | FORGE_SDK_RUNTIME | FORGE_SDK_VALIDATION,
                                  FORGE_SDK_RUNTIME,
                                  FORGE_SDK_PHYSICS,
                                  FORGE_SDK_PHYSICS | FORGE_SDK_DIAGNOSTICS,
                                  &ecs_init,
                                  &ecs_os_api,
                                  schema,
                                  start,
                                  nullptr,
                                  nullptr,
                                  nullptr,
                                  nullptr};
} // namespace
extern "C" FORGE_SDK_EXPORT const ForgeNativeSdkV1* FORGE_SDK_CALL forge_native_sdk_v1() {
    return &descriptor;
}
