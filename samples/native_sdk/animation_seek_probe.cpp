#include "../projects/FeatureGallery/CodeExamples/ClipSeek.hpp"
#include <cmath>
#include <cstdio>
#include <flecs.h>
#include <forge/animation_components.hpp>
#include <forge/native_sdk_identity.h>
#include <limits>
#include <stdexcept>
#include <thread>
namespace {
struct SeekProbe {
    std::uint32_t ticks = 0;
    double time = 0, movement = 0;
};
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
int32_t FORGE_SDK_CALL schema(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    try {
        flecs::world world(host->world);
        world.component<forge::Animator>("forge.animator");
        auto type = world.component<SeekProbe>("project.SeekProbe")
                        .member<std::uint32_t>("ticks")
                        .member<double>("time")
                        .member<double>("movement");
        return host->authoring_type(host->context, type, "project.seek_probe", 1,
                                    R"({"ticks":0,"time":0,"movement":0})", "Animation", error,
                                    capacity);
    } catch (const std::exception& e) {
        if (capacity)
            std::snprintf(error, capacity, "%s", e.what());
        return 0;
    }
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char*, uint32_t) {
    flecs::world world(host->world);
    world.system<const forge::Animator, SeekProbe>("project.SeekIncoming")
        .kind(host->fixed_phase)
        .each([host](flecs::entity entity, const forge::Animator&, SeekProbe& probe) {
            const forge::sdk::Client client(host);
            ForgeSdkEntityRefV1 ref{};
            check(client.entity_reference(entity.id(), ref) == 1,
                  "Seek entity reference unavailable");
            const auto tick = ++probe.ticks;
            if (tick == 1) {
                check(client.animation_control(ref.scene_uuid, ref.entity_uuid,
                                               FORGE_SDK_ANIMATION_PAUSE) == 1,
                      "Seek pause failed");
                check(forge_seek_clip(host, entity.id(), .75), "Ordinary C++ seek example failed");
            } else if (tick == 2) {
                check(client.animation_control(ref.scene_uuid, ref.entity_uuid,
                                               FORGE_SDK_ANIMATION_PLAY) == 1,
                      "Seek resume failed");
                check(forge_seek_clip(host, entity.id(), .9), "Playing SDK seek failed");
            } else if (tick == 3)
                check(forge_seek_clip(host, entity.id(), 1), "SDK loop end seek failed");
            else if (tick == 4) {
                check(client.animation_control(ref.scene_uuid, ref.entity_uuid,
                                               FORGE_SDK_ANIMATION_PAUSE) == 1,
                      "Second seek pause failed");
                check(forge_seek_clip(host, entity.id(), .25), "Second paused seek failed");
            } else if (tick == 5)
                check(client.animation_control(ref.scene_uuid, ref.entity_uuid,
                                               FORGE_SDK_ANIMATION_RESTART) == 1,
                      "SDK restart after seek failed");
            if (tick == 6) {
                for (double invalid : {-1., 1.1, std::numeric_limits<double>::infinity()})
                    check(client.animation_seek(ref.scene_uuid, ref.entity_uuid, invalid) == -1,
                          "SDK accepted invalid seek seconds");
                check(client.animation_seek("invalid", ref.entity_uuid, .5) == -1,
                      "SDK accepted invalid seek UUID");
                check(client.animation_seek(ref.scene_uuid, "00000000-0000-4000-8000-000000000002",
                                            .5) == 0,
                      "SDK accepted missing seek player");
                int foreign = 9, wrapped = 9;
                std::thread thread([&] {
                    wrapped = client.animation_seek(ref.scene_uuid, ref.entity_uuid, .5);
                    foreign =
                        host->animation_seek(host->context, ref.scene_uuid, ref.entity_uuid, .5);
                });
                thread.join();
                check(wrapped == 0 && foreign == -1, "SDK accepted foreign-thread seek");
            }
        })
        .add(host->fixed_tag);
    world.system<const forge::Animator, SeekProbe>("project.SeekObservation")
        .kind(host->post_animation_phase)
        .each([host](flecs::entity entity, const forge::Animator&, SeekProbe& probe) {
            const forge::sdk::Client client(host);
            ForgeSdkEntityRefV1 ref{};
            ForgeSdkAnimationPlaybackV1 status{};
            ForgeSdkAnimationRootMotionV1 motion{};
            check(client.entity_reference(entity.id(), ref) == 1 &&
                      client.animation_playback(ref.scene_uuid, ref.entity_uuid, status) == 1 &&
                      client.animation_root_motion(ref.scene_uuid, ref.entity_uuid, motion) == 1,
                  "SDK seek observations unavailable");
            probe.time = status.time;
            probe.movement += motion.translation[2];
            check(client.animation_seek(ref.scene_uuid, ref.entity_uuid, .5) == 0,
                  "SDK accepted PostAnimation seek");
        })
        .add(host->fixed_tag);
    return 1;
}
const char* dependencies[] = {"forge.animation", "forge.input"};
const ForgeNativeSdkV1 api = {sizeof(ForgeNativeSdkV1),
                              FORGE_NATIVE_SDK_ABI,
                              FORGE_NATIVE_SDK_FINGERPRINT,
                              "project.animation_seek_probe",
                              "1",
                              dependencies,
                              2,
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
