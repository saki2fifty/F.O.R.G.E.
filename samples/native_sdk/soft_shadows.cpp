#include <cstdio>
#include <exception>
#include <flecs.h>
#include <forge/native_sdk.h>
#include <forge/native_sdk_identity.h>
#include <forge/render_components.hpp>
namespace {
struct ShadowControl {
    std::uint32_t ticks = 0;
};
int32_t FORGE_SDK_CALL schema(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    try {
        flecs::world world(host->world);
        world.component<forge::Light>("forge.light");
        const auto type =
            world.component<ShadowControl>("project.ShadowControl").member<std::uint32_t>("ticks");
        return host->authoring_type(host->context, type, "project.shadow_control", 1,
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
        world.system<ShadowControl, forge::Light>("project.TuneSoftShadows")
            .kind(host->fixed_phase)
            .each([](ShadowControl& control, forge::Light& light) {
                ++control.ticks;
                if (control.ticks > 5)
                    return;
                // Exercise the installed Light layout across all five source kinds.
                light.cast_shadows = control.ticks <= 3;
                light.kind = (control.ticks - 1) % 5;
                light.shadow_source_radius = .125f * control.ticks;
                light.shadow_angular_radius = .01f * control.ticks;
                light.emitter_radius = .25f * control.ticks;
                light.emitter_width = .5f * control.ticks;
                light.emitter_height = .75f * control.ticks;
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
                              "project.soft_shadow_example",
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
