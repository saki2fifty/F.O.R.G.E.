#include <cstdio>
#include <exception>
#include <flecs.h>
#include <forge/decal.hpp>
#include <forge/native_sdk.h>
#include <forge/native_sdk_identity.h>
namespace {
struct DecalControl {
    std::uint32_t ticks = 0;
};
int32_t FORGE_SDK_CALL schema(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    try {
        flecs::world world(host->world);
        world.component<forge::DecalProjector>("forge.decal");
        const auto type =
            world.component<DecalControl>("project.DecalControl").member<std::uint32_t>("ticks");
        return host->authoring_type(host->context, type, "project.decal_control", 1,
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
        world.system<DecalControl, forge::DecalProjector>("project.TuneProjectedColor")
            .kind(host->fixed_phase)
            .each([](DecalControl& control, forge::DecalProjector& decal) {
                if (++control.ticks > 3)
                    return;
                // The assigned Material remains shared; these are instance values.
                decal.enabled = control.ticks != 2;
                decal.width = 2 + .25f * control.ticks;
                decal.height = 3 + .5f * control.ticks;
                decal.depth = 4 + .25f * control.ticks;
                decal.tint_r = .125f * control.ticks;
                decal.tint_g = .25f;
                decal.tint_b = .5f;
                decal.opacity = .25f * control.ticks;
                decal.angle_fade_start = .25f * control.ticks;
                decal.angle_fade_end = 1.25f;
                decal.layers = 1u << control.ticks;
                decal.priority = -std::int32_t(control.ticks);
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
                              "project.decal_example",
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
