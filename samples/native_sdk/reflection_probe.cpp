#include <cstdio>
#include <exception>
#include <flecs.h>
#include <forge/native_sdk.h>
#include <forge/native_sdk_identity.h>
#include <forge/reflection_probe.hpp>
namespace {
struct ProbeControl {
    std::uint32_t ticks = 0;
};
int32_t FORGE_SDK_CALL schema(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    try {
        flecs::world world(host->world);
        world.component<forge::ReflectionProbe>("forge.reflection_probe");
        const auto type =
            world.component<ProbeControl>("project.ProbeControl").member<std::uint32_t>("ticks");
        return host->authoring_type(host->context, type, "project.probe_control", 1,
                                    R"({"ticks":0})", "Rendering", error, capacity);
    } catch (const std::exception& exception) {
        if (capacity)
            std::snprintf(error, capacity, "%s", exception.what());
        return 0;
    }
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    try {
        flecs::world world(host->world);
        world.system<ProbeControl, forge::ReflectionProbe>("project.TuneReflectionMap")
            .kind(host->fixed_phase)
            .each([](ProbeControl& control, forge::ReflectionProbe& probe) {
                ++control.ticks;
                if (control.ticks > 3)
                    return;
                // Retain the assigned map; tune only the existing local volume.
                probe.intensity = 1 + .5 * control.ticks;
                probe.rotation_degrees = 15 * control.ticks;
                probe.half_extent_x = 2 + .25 * control.ticks;
                probe.priority = std::int32_t(control.ticks);
                probe.box_projection = control.ticks != 2;
            })
            .add(host->fixed_tag);
        return 1;
    } catch (const std::exception& exception) {
        if (capacity)
            std::snprintf(error, capacity, "%s", exception.what());
        return 0;
    }
}
const ForgeNativeSdkV1 module{sizeof(ForgeNativeSdkV1),
                              FORGE_NATIVE_SDK_ABI,
                              FORGE_NATIVE_SDK_FINGERPRINT,
                              "project.reflection_probe_example",
                              "1",
                              nullptr,
                              0,
                              FORGE_SDK_AUTHORING | FORGE_SDK_RUNTIME | FORGE_SDK_VALIDATION,
                              FORGE_SDK_RUNTIME,
                              0,
                              0,
                              &ecs_init,
                              &ecs_os_api,
                              schema,
                              start,
                              nullptr};
} // namespace
extern "C" FORGE_SDK_EXPORT const ForgeNativeSdkV1* FORGE_SDK_CALL forge_native_sdk_v1() {
    return &module;
}
