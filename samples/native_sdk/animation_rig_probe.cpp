#include "../projects/FeatureGallery/CodeExamples/SetRigTarget.hpp"
#include <cstdio>
#include <forge/native_sdk.h>
#include <forge/native_sdk_identity.h>
namespace {
struct RigProbe {
    std::uint32_t ticks = 0;
};
int32_t FORGE_SDK_CALL schema(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    try {
        flecs::world world(host->world);
        world.component<forge::TwoBoneIK>("forge.two_bone_ik");
        world.component<forge::AimIK>("forge.aim_ik");
        const auto type =
            world.component<RigProbe>("project.RigProbe").member<std::uint32_t>("ticks");
        return host->authoring_type(host->context, type, "project.rig_probe", 1, R"({"ticks":0})",
                                    "Animation", error, capacity);
    } catch (const std::exception& e) {
        if (capacity)
            std::snprintf(error, capacity, "%s", e.what());
        return 0;
    }
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char*, uint32_t) {
    flecs::world world(host->world);
    world.system<RigProbe>("project.ChangeRigTarget")
        .kind(host->fixed_phase)
        .each([](flecs::entity actor, RigProbe& probe) {
            ++probe.ticks;
            const float x = .7f + float(probe.ticks) * .005f;
            if (!forge_set_rig_target<forge::TwoBoneIK>(actor, {x, 1.1f, 0}))
                throw std::runtime_error("Ordinary C++ rig target helper rejected");
        })
        .add(host->fixed_tag);
    return 1;
}
const char* dependencies[] = {"forge.animation"};
const ForgeNativeSdkV1 api = {sizeof(ForgeNativeSdkV1),
                              FORGE_NATIVE_SDK_ABI,
                              FORGE_NATIVE_SDK_FINGERPRINT,
                              "project.animation_rig_probe",
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
