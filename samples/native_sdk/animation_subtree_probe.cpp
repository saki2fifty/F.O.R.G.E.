#include "../projects/FeatureGallery/CodeExamples/SetBlendSubtree.hpp"
#include <cstdio>
#include <forge/native_sdk.h>
#include <forge/native_sdk_identity.h>
#include <string>
namespace {
struct SubtreeProbe {
    std::uint32_t ticks = 0;
    std::string upper;
};
int32_t FORGE_SDK_CALL schema(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    try {
        flecs::world world(host->world);
        world.component<forge::Animator>("forge.animator");
        auto type = world.component<SubtreeProbe>("project.SubtreeProbe")
                        .member<std::uint32_t>("ticks")
                        .member<std::string>("upper");
        return host->authoring_type(host->context, type, "project.subtree_probe", 1,
                                    R"({"ticks":0,"upper":""})", "Animation", error, capacity);
    } catch (const std::exception& e) {
        if (capacity)
            std::snprintf(error, capacity, "%s", e.what());
        return 0;
    }
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char*, uint32_t) {
    flecs::world world(host->world);
    world.system<const forge::Animator, SubtreeProbe>("project.ChangeSubtree")
        .kind(host->fixed_phase)
        .each([](flecs::entity actor, const forge::Animator& animator, SubtreeProbe& probe) {
            ++probe.ticks;
            if (probe.upper.empty())
                probe.upper = animator.blend_root.id.str();
            if (probe.ticks == 5)
                forge_set_blend_subtree(actor, {});
            if (probe.ticks == 10)
                forge_set_blend_subtree(actor, {forge::AssetId::parse(probe.upper)});
        })
        .add(host->fixed_tag);
    return 1;
}
const char* dependencies[] = {"forge.animation"};
const ForgeNativeSdkV1 api = {sizeof(ForgeNativeSdkV1),
                              FORGE_NATIVE_SDK_ABI,
                              FORGE_NATIVE_SDK_FINGERPRINT,
                              "project.animation_subtree_probe",
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
