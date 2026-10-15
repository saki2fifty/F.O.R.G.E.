#include <MaterialAnimation.hpp>
#include <algorithm>
#include <cstdio>
#include <forge/native_sdk.h>
#include <forge/native_sdk_identity.h>
#include <forge/sdk_client.hpp>
#include <stdexcept>
namespace {
struct MaterialAnimationProbe {
    std::uint32_t ticks = 0;
    double time = 0;
    float roughness = 0;
};
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
int32_t FORGE_SDK_CALL schema(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    try {
        flecs::world world(host->world);
        world.component<forge::MeshRenderer>("forge.mesh_renderer");
        world.component<forge::TransformAnimator>("forge.transform_animator");
        const auto type = world.component<MaterialAnimationProbe>("project.MaterialAnimationProbe")
                              .member<std::uint32_t>("ticks")
                              .member<double>("time")
                              .member<float>("roughness");
        return host->authoring_type(host->context, type, "project.material_animation_probe", 1,
                                    R"({"ticks":0,"time":0,"roughness":0})", "Animation", error,
                                    capacity);
    } catch (const std::exception& why) {
        if (capacity)
            std::snprintf(error, capacity, "%s", why.what());
        return 0;
    }
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char*, uint32_t) {
    flecs::world world(host->world);
    world.system<MaterialAnimationProbe>("project.ControlMaterialAnimation")
        .kind(host->fixed_phase)
        .each([host](flecs::entity object, MaterialAnimationProbe& probe) {
            const forge::sdk::Client client(host);
            ForgeSdkEntityRefV1 ref{};
            check(client.entity_reference(object.id(), ref) == 1, "Missing material animation ref");
            const auto tick = ++probe.ticks;
            if (tick == 1) {
                check(client.animation_control(ref.scene_uuid, ref.entity_uuid,
                                               FORGE_SDK_ANIMATION_PAUSE) == 1,
                      "C++ material animation pause failed");
                check(client.animation_seek(ref.scene_uuid, ref.entity_uuid, .5) == 1,
                      "C++ material animation seek failed");
            } else if (tick == 2) {
                check(forge_set_material_animation_speed(object, .5f),
                      "C++ material animation speed example failed");
                check(!forge_set_material_animation_speed(object, -1),
                      "C++ material animation example accepted a negative speed");
                check(client.animation_control(ref.scene_uuid, ref.entity_uuid,
                                               FORGE_SDK_ANIMATION_PLAY) == 1 &&
                          client.animation_seek(ref.scene_uuid, ref.entity_uuid, 1) == 1,
                      "C++ material animation resume/seek failed");
            } else if (tick == 8)
                check(client.animation_control(ref.scene_uuid, ref.entity_uuid,
                                               FORGE_SDK_ANIMATION_RESTART) == 1,
                      "C++ material animation restart failed");
        })
        .add(host->fixed_tag);
    world
        .system<const forge::MeshRenderer, MaterialAnimationProbe>(
            "project.ObserveMaterialAnimation")
        .kind(host->post_animation_phase)
        .each([host](flecs::entity object, const forge::MeshRenderer& renderer,
                     MaterialAnimationProbe& probe) {
            const forge::sdk::Client client(host);
            ForgeSdkEntityRefV1 ref{};
            ForgeSdkAnimationPlaybackV1 status{};
            check(client.entity_reference(object.id(), ref) == 1 &&
                      client.animation_playback(ref.scene_uuid, ref.entity_uuid, status) == 1,
                  "C++ material animation status failed");
            probe.time = status.time;
            const auto at = std::find_if(
                renderer.material_parameters.begin(), renderer.material_parameters.end(),
                [](const auto& value) { return value.key == "roughnessFactor"; });
            check(at != renderer.material_parameters.end(), "C++ material animation value missing");
            probe.roughness = at->parameter.value[0];
        })
        .add(host->fixed_tag);
    return 1;
}
const char* dependencies[] = {"forge.animation"};
const ForgeNativeSdkV1 api{sizeof(ForgeNativeSdkV1),
                           FORGE_NATIVE_SDK_ABI,
                           FORGE_NATIVE_SDK_FINGERPRINT,
                           "project.material_animation_probe",
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
