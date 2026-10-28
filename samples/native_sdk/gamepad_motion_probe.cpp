// Public SDK wrapper for the single authoritative original Gallery behavior.
#include <cstdio>
#include <exception>
#include <flecs.h>
#include <forge/native_sdk.h>
#include <forge/native_sdk_identity.h>
void forge_register_system_GamepadMotion(const ForgeSdkWorldV1*);
void forge_control_GamepadMotion(const ForgeSdkWorldV1*);
namespace {
int32_t FORGE_SDK_CALL schemas(const ForgeSdkWorldV1*, char*, uint32_t) { return 1; }
template <class F> int32_t invoke(F call, char* error, uint32_t size) {
    try {
        call();
        return 1;
    } catch (const std::exception& value) {
        if (size)
            std::snprintf(error, size, "%s", value.what());
    } catch (...) {
        if (size)
            std::snprintf(error, size, "Motion example failed");
    }
    return 0;
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char* error, uint32_t size) {
    return invoke([&] { forge_register_system_GamepadMotion(host); }, error, size);
}
int32_t FORGE_SDK_CALL controls(const ForgeSdkWorldV1* host, char* error, uint32_t size) {
    return invoke([&] { forge_control_GamepadMotion(host); }, error, size);
}
const char* dependencies[]{"forge.input", "forge.transforms", "forge.game"};
const ForgeNativeSdkV1 api{sizeof(ForgeNativeSdkV1),
                           FORGE_NATIVE_SDK_ABI,
                           FORGE_NATIVE_SDK_FINGERPRINT,
                           "project.gamepad_motion",
                           "1",
                           dependencies,
                           3,
                           FORGE_SDK_RUNTIME | FORGE_SDK_VALIDATION,
                           FORGE_SDK_RUNTIME,
                           0,
                           FORGE_SDK_GAME,
                           &ecs_init,
                           &ecs_os_api,
                           schemas,
                           start,
                           nullptr,
                           controls};
} // namespace
extern "C" FORGE_SDK_EXPORT const ForgeNativeSdkV1* FORGE_SDK_CALL forge_native_sdk_v1() {
    return &api;
}
