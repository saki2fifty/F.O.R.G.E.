#include "../projects/FeatureGallery/CodeExamples/SetAnimationMix.hpp"
#include "../projects/FeatureGallery/CodeExamples/SetBlendSubtree.hpp"
#include <cstdio>
#include <forge/native_sdk.h>
#include <forge/native_sdk_identity.h>
#include <string>
namespace {
struct OverlayProbe {
    std::uint32_t ticks = 0;
    std::string upper;
};
int32_t FORGE_SDK_CALL schema(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    try {
        flecs::world world(host->world);
        world.component<forge::Animator>("forge.animator");
        const auto type = world.component<OverlayProbe>("project.OverlayProbe")
                              .member<std::uint32_t>("ticks")
                              .member<std::string>("upper");
        return host->authoring_type(host->context, type, "project.overlay_probe", 1,
                                    R"({"ticks":0,"upper":""})", "Animation", error, capacity);
    } catch (const std::exception& e) {
        if (capacity)
            std::snprintf(error, capacity, "%s", e.what());
        return 0;
    }
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char*, uint32_t) {
    flecs::world world(host->world);
    world.system<OverlayProbe>("project.ChangeAnimationMix")
        .kind(host->fixed_phase)
        .each([](flecs::entity actor, OverlayProbe& probe) {
            ++probe.ticks;
            bool valid = true;
            if (probe.ticks == 5)
                valid = forge_set_animation_mix(actor, 0, 1, 0);
            if (probe.ticks == 10)
                valid = forge_set_animation_mix(actor, 1, .5f, 0);
            if (probe.ticks == 15)
                forge_set_blend_subtree(actor, {forge::AssetId::parse(probe.upper)});
            if (probe.ticks == 20) {
                forge_set_blend_subtree(actor, {});
                valid = forge_set_animation_mix(actor, 1, 1, 0);
            }
            if (!valid)
                throw std::runtime_error("Ordinary C++ animation mix helper rejected");
        })
        .add(host->fixed_tag);
    return 1;
}
const char* dependencies[] = {"forge.animation"};
const ForgeNativeSdkV1 api = {sizeof(ForgeNativeSdkV1),
                              FORGE_NATIVE_SDK_ABI,
                              FORGE_NATIVE_SDK_FINGERPRINT,
                              "project.animation_overlay_probe",
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
