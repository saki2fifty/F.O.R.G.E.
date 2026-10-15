#include <SetBlendPosition.hpp>
#include <cstdio>
#include <forge/native_sdk.h>
#include <forge/native_sdk_identity.h>
#include <string>
namespace {
struct BlendSpaceProbe {
    std::uint32_t ticks = 0;
    bool plane = false;
};
int32_t FORGE_SDK_CALL schema(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    try {
        flecs::world world(host->world);
        world.component<forge::Animator>("forge.animator");
        const auto type = world.component<BlendSpaceProbe>("project.BlendSpaceProbe")
                              .member<std::uint32_t>("ticks")
                              .member<bool>("plane");
        return host->authoring_type(host->context, type, "project.blend_space_probe", 1,
                                    R"({"ticks":0,"plane":false})", "Animation", error, capacity);
    } catch (const std::exception& e) {
        if (capacity)
            std::snprintf(error, capacity, "%s", e.what());
        return 0;
    }
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char*, uint32_t) {
    flecs::world world(host->world);
    world.system<BlendSpaceProbe>("project.ChangeBlendPosition")
        .kind(host->fixed_phase)
        .each([](flecs::entity actor, BlendSpaceProbe& probe) {
            ++probe.ticks;
            if (probe.plane) {
                const auto tick = probe.ticks;
                const float x = tick < 5    ? .5f
                                : tick < 10 ? .75f
                                : tick < 15 ? 1.5f
                                : tick < 20 ? 0.f
                                : tick < 25 ? 2.f
                                            : 0.f;
                const float y = tick < 5    ? .5f
                                : tick < 10 ? .25f
                                : tick < 15 ? 1.5f
                                : tick < 25 ? 0.f
                                            : 2.f;
                if (!forge_set_blend_position(actor, x, y))
                    throw std::runtime_error("Ordinary C++ XY blend-position helper rejected");
                return;
            }
            bool valid = true;
            if (probe.ticks == 5)
                valid = forge_set_blend_position(actor, 0);
            if (probe.ticks == 10)
                valid = forge_set_blend_position(actor, .5f);
            if (probe.ticks == 11)
                valid = forge_set_blend_position(actor, .75f);
            if (probe.ticks == 12)
                valid = forge_set_blend_position(actor, .25f);
            if (probe.ticks == 13)
                valid = forge_set_blend_position(actor, .5f);
            if (probe.ticks == 15)
                valid = forge_set_blend_position(actor, 2);
            if (probe.ticks == 20)
                valid = forge_set_blend_position(actor, 3);
            if (probe.ticks == 25)
                valid = forge_set_blend_position(actor, -1);
            if (!valid)
                throw std::runtime_error("Ordinary C++ blend-position helper rejected");
        })
        .add(host->fixed_tag);
    return 1;
}
const char* dependencies[] = {"forge.animation"};
const ForgeNativeSdkV1 api = {sizeof(ForgeNativeSdkV1),
                              FORGE_NATIVE_SDK_ABI,
                              FORGE_NATIVE_SDK_FINGERPRINT,
                              "project.animation_blend_space_probe",
                              "1",
                              dependencies,
                              1,
                              FORGE_SDK_AUTHORING | FORGE_SDK_RUNTIME | FORGE_SDK_VALIDATION,
                              FORGE_SDK_RUNTIME,
                              FORGE_SDK_ANIMATION,
                              FORGE_SDK_ANIMATION,
                              &ecs_init,
                              &ecs_os_api,
                              schema,
                              start,
                              nullptr};
} // namespace
extern "C" FORGE_SDK_EXPORT const ForgeNativeSdkV1* FORGE_SDK_CALL forge_native_sdk_v1() {
    return &api;
}
