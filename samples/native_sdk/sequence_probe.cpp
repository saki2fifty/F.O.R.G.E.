#include <cmath>
#include <cstdio>
#include <flecs.h>
#include <forge/animation_components.hpp>
#include <forge/audio_components.hpp>
#include <forge/native_sdk_identity.h>
#include <forge/render_components.hpp>
#include <forge/sdk_client.hpp>
#include <limits>
#include <stdexcept>
#include <thread>
namespace {
struct SequenceProbe {
    std::uint32_t ticks = 0;
    double time = 0;
};
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
int32_t FORGE_SDK_CALL schema(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    try {
        flecs::world world(host->world);
        world.component<forge::AnimationSequence>("forge.animation_sequence");
        world.component<forge::TransformAnimator>("forge.transform_animator");
        world.component<forge::CameraShot>("forge.camera_shot");
        world.component<forge::AudioCue>("forge.audio_cue");
        world.component<forge::AudioSource>("forge.audio_source");
        world.component<forge::Camera>("forge.camera");
        auto type = world.component<SequenceProbe>("project.SequenceProbe")
                        .member<std::uint32_t>("ticks")
                        .member<double>("time");
        return host->authoring_type(host->context, type, "project.sequence_probe", 1,
                                    R"({"ticks":0,"time":0})", "Animation", error, capacity);
    } catch (const std::exception& e) {
        if (capacity)
            std::snprintf(error, capacity, "%s", e.what());
    } catch (...) {
        if (capacity)
            std::snprintf(error, capacity, "Sequence schema registration failed");
    }
    return 0;
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    try {
        flecs::world world(host->world);
        world.system<const forge::AnimationSequence, SequenceProbe>("project.SequenceControls")
            .kind(host->fixed_phase)
            .each([host](flecs::entity entity, const forge::AnimationSequence& config,
                         SequenceProbe& probe) {
                const forge::sdk::Client client(host);
                ForgeSdkEntityRefV1 ref{};
                check(client.entity_reference(entity.id(), ref) == 1,
                      "Sequence controller reference unavailable");
                ForgeSdkAnimationPlaybackV1 admitted{};
                if (client.animation_playback(ref.scene_uuid, ref.entity_uuid, admitted) == 0)
                    return; // Direct host may still be admitting the selected clip resources.
                auto control = [&](std::uint32_t operation) {
                    check(client.animation_control(ref.scene_uuid, ref.entity_uuid, operation) == 1,
                          "C++ sequence control failed");
                };
                auto seek = [&](double time) {
                    check(client.animation_seek(ref.scene_uuid, ref.entity_uuid, time) == 1,
                          "C++ sequence seek failed");
                };
                const auto tick = ++probe.ticks;
                if (tick == 1) {
                    control(FORGE_SDK_ANIMATION_PAUSE);
                    seek(2.5);
                } else if (tick == 2) {
                    control(FORGE_SDK_ANIMATION_PLAY);
                    seek(.5);
                } else if (tick == 3) {
                    flecs::world current(host->world);
                    unsigned tracks = 0;
                    current.each([&](flecs::entity child, const forge::TransformAnimator& player) {
                        if (player.sequence.entity.str() != ref.entity_uuid)
                            return;
                        ForgeSdkEntityRefV1 child_ref{};
                        ForgeSdkAnimationPlaybackV1 status{};
                        check(client.entity_reference(child.id(), child_ref) == 1 &&
                                  client.animation_playback(child_ref.scene_uuid,
                                                            child_ref.entity_uuid, status) == 1 &&
                                  status.clip_uuid[0] != 0,
                              "Bound track status unavailable through exact SDK");
                        check(client.animation_control(child_ref.scene_uuid, child_ref.entity_uuid,
                                                       FORGE_SDK_ANIMATION_PAUSE) == 0 &&
                                  client.animation_seek(child_ref.scene_uuid, child_ref.entity_uuid,
                                                        .25) == 0,
                              "C++ independently controlled a bound track");
                        ++tracks;
                    });
                    unsigned shots = 0;
                    current.each([&](const forge::CameraShot& shot) {
                        if (shot.sequence.entity.str() != ref.entity_uuid)
                            return;
                        bool found = false;
                        current.each([&](flecs::entity camera, const forge::Camera& lens) {
                            ForgeSdkEntityRefV1 camera_ref{};
                            if (client.entity_reference(camera.id(), camera_ref) == 1 &&
                                shot.camera.scene.str() == camera_ref.scene_uuid &&
                                shot.camera.entity.str() == camera_ref.entity_uuid) {
                                check(!lens.enabled, "Camera Shot rewrote native Camera.enabled "
                                                     "across SDK boundary");
                                found = true;
                            }
                        });
                        check(found && shot.enabled && shot.start_seconds < shot.end_seconds,
                              "Camera Shot typed data/reference unavailable through matching SDK");
                        ++shots;
                    });
                    unsigned cues = 0;
                    current.each([&](const forge::AudioCue& cue) {
                        if (cue.sequence.entity.str() != ref.entity_uuid)
                            return;
                        bool found = false;
                        current.each([&](flecs::entity actor, const forge::AudioSource& source) {
                            ForgeSdkEntityRefV1 source_ref{};
                            if (client.entity_reference(actor.id(), source_ref) == 1 &&
                                cue.source.scene.str() == source_ref.scene_uuid &&
                                cue.source.entity.str() == source_ref.entity_uuid) {
                                check(bool(source.clip.id) && !source.play_on_start && !source.loop,
                                      "Cue source settings changed across SDK boundary");
                                found = true;
                            }
                        });
                        check(found && cue.enabled && cue.time_seconds >= 0 &&
                                  cue.time_seconds <= config.duration,
                              "Audio Cue typed data/reference unavailable through matching SDK");
                        ++cues;
                    });
                    check((cues == 0 && shots == 0 && tracks == 3) ||
                              (cues == 0 && shots == 3 && tracks == 1) ||
                              (cues == 3 && shots == 2 && tracks == 1),
                          "Sequence probe did not inspect its complete object/shot tracks");
                } else if (tick == 4) {
                    control(FORGE_SDK_ANIMATION_PAUSE);
                    seek(config.duration);
                } else if (tick == 5)
                    control(FORGE_SDK_ANIMATION_RESTART);
                else if (tick == 6) {
                    control(FORGE_SDK_ANIMATION_PAUSE);
                    seek(.75);
                    for (double invalid :
                         {-1., config.duration + 1, std::numeric_limits<double>::infinity()})
                        check(client.animation_seek(ref.scene_uuid, ref.entity_uuid, invalid) == -1,
                              "C++ admitted an invalid sequence seek");
                    int wrapped = 9, direct = 9;
                    std::thread foreign([&] {
                        wrapped = client.animation_seek(ref.scene_uuid, ref.entity_uuid, .5);
                        direct = host->animation_seek(host->context, ref.scene_uuid,
                                                      ref.entity_uuid, .5);
                    });
                    foreign.join();
                    check(wrapped == 0 && direct == -1,
                          "Sequence seek bypassed owner-thread guards");
                } else if (tick == 7)
                    control(FORGE_SDK_ANIMATION_PLAY);
            })
            .add(host->fixed_tag);
        world.system<const forge::AnimationSequence, SequenceProbe>("project.SequenceObservation")
            .kind(host->post_animation_phase)
            .each([host](flecs::entity entity, const forge::AnimationSequence&,
                         SequenceProbe& probe) {
                if (!probe.ticks)
                    return;
                const forge::sdk::Client client(host);
                ForgeSdkEntityRefV1 ref{};
                ForgeSdkAnimationPlaybackV1 status{};
                check(client.entity_reference(entity.id(), ref) == 1 &&
                          client.animation_playback(ref.scene_uuid, ref.entity_uuid, status) == 1,
                      "C++ sequence observation unavailable");
                check(status.clip_uuid[0] == 0 && status.duration == 6,
                      "Controller status fabricated a clip identity");
                check(client.animation_seek(ref.scene_uuid, ref.entity_uuid, 1) == 0,
                      "Sequence seek admitted after Animation phase");
                probe.time = status.time;
            })
            .add(host->fixed_tag);
        return 1;
    } catch (const std::exception& e) {
        if (capacity)
            std::snprintf(error, capacity, "%s", e.what());
    } catch (...) {
        if (capacity)
            std::snprintf(error, capacity, "Sequence systems failed to start");
    }
    return 0;
}
const char* dependencies[] = {"forge.animation"};
const ForgeNativeSdkV1 api = {sizeof(ForgeNativeSdkV1),
                              FORGE_NATIVE_SDK_ABI,
                              FORGE_NATIVE_SDK_FINGERPRINT,
                              "project.sequence_probe",
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
