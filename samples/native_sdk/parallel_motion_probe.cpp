#include <cstdio>
#include <exception>
#include <flecs.h>
#include <forge/native_sdk.h>
#include <forge/native_sdk_identity.h>
int32_t forge_register_component_ParallelMotion(const ForgeSdkWorldV1*, char*, uint32_t);
void forge_register_system_ParallelMotion(const ForgeSdkWorldV1*);
namespace {
int32_t FORGE_SDK_CALL schemas(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    return forge_register_component_ParallelMotion(host, error, capacity);
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    try {
        forge_register_system_ParallelMotion(host);
        return 1;
    } catch (const std::exception& exception) {
        if (capacity)
            std::snprintf(error, capacity, "%s", exception.what());
    } catch (...) {
        if (capacity)
            std::snprintf(error, capacity, "Parallel Motion system registration failed");
    }
    return 0;
}
const char* dependencies[]{"forge.transforms"};
const ForgeNativeSdkV1 api{sizeof(ForgeNativeSdkV1),
                           FORGE_NATIVE_SDK_ABI,
                           FORGE_NATIVE_SDK_FINGERPRINT,
                           "project.parallel_motion",
                           "1",
                           dependencies,
                           1,
                           FORGE_SDK_RUNTIME | FORGE_SDK_VALIDATION,
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
