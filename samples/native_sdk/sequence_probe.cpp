#include <algorithm>
#include <cmath>
#include <cstdio>
#include <flecs.h>
#include <forge/animation_components.hpp>
#include <forge/audio_components.hpp>
#include <forge/native_sdk_identity.h>
#include <forge/render_components.hpp>
#include <forge/sdk_client.hpp>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
namespace {
static_assert(forge::skeletal_sequence_runtime_profile_version == 1);
static_assert(forge::sequence_rate_runtime_profile_version == 1);
static_assert(forge::sequence_loop_runtime_profile_version == 1);
static_assert(forge::sequence_arrangement_runtime_profile_version == 1);
static_assert(forge::sequence_clip_limit == 8);
static_assert(forge::sequence_placement_limit == 8);
static_assert(forge::sequence_placement_runtime_profile_version == 1);
struct SequenceProbe {
    std::uint32_t ticks = 0;
    double time = 0, blend = 0;
};
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class Player>
void check_collection(const Player& player, const ForgeSdkAnimationPlaybackV1& status,
                      double step) {
    check(player.sequence_extra_clips.size() == 6 && player.sequence_start_seconds == 0 &&
              player.sequence_next_start_seconds == step &&
              std::abs(status.duration - 9 * step) < 1e-9,
          "Eight-clip native values/span differ through matching SDK");
    for (std::size_t i = 0; i < player.sequence_extra_clips.size(); ++i) {
        const auto& extra = player.sequence_extra_clips[i];
        check(extra.start_seconds == (i + 2) * step &&
                  extra.clip == (i % 2 ? player.sequence_next_clip : player.clip),
              "Typed native clip placement lost its time or asset identity");
    }
    const auto active = std::clamp(int(std::floor(status.time / step)) - 1, 0, 6);
    const auto weight = std::clamp((status.time - (active + 1) * step) / step, 0., 1.);
    check((active % 2 ? player.sequence_next_clip : player.clip).id.str() == status.clip_uuid &&
              (active % 2 ? player.clip : player.sequence_next_clip).id.str() ==
                  status.blend_clip_uuid &&
              std::abs(status.blend_weight - weight) < 1e-9,
          "Eight-clip active IDs or independent phase differ through matching SDK");
}
int32_t FORGE_SDK_CALL schema(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    try {
        flecs::world world(host->world);
        world.component<forge::AnimationSequence>("forge.animation_sequence");
        world.component<forge::TransformAnimator>("forge.transform_animator");
        world.component<forge::Animator>("forge.animator");
        world.component<forge::CameraShot>("forge.camera_shot");
        world.component<forge::AudioCue>("forge.audio_cue");
        world.component<forge::AudioSource>("forge.audio_source");
        world.component<forge::Camera>("forge.camera");
        auto type = world.component<SequenceProbe>("project.SequenceProbe")
                        .member<std::uint32_t>("ticks")
                        .member<double>("time")
                        .member<double>("blend");
        return host->authoring_type(host->context, type, "project.sequence_probe", 1,
                                    R"({"ticks":0,"time":0,"blend":0})", "Animation", error,
                                    capacity);
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
                    std::map<std::string, ForgeSdkAnimationPlaybackV1> controllers{
                        {ref.entity_uuid, admitted}};
                    unsigned nested = 0;
                    current.each([&](flecs::entity child,
                                     const forge::AnimationSequence& placement) {
                        if (placement.sequence.entity.str() != ref.entity_uuid)
                            return;
                        ForgeSdkEntityRefV1 child_ref{};
                        ForgeSdkAnimationPlaybackV1 status{};
                        const double unwrapped =
                            std::max(0., (admitted.time - 1) * placement.sequence_rate);
                        double local = std::min(unwrapped, placement.duration);
                        if (placement.sequence_loop && unwrapped > 0) {
                            local = std::fmod(unwrapped, placement.duration);
                            if (admitted.time == config.duration && local == 0)
                                local = placement.duration;
                        }
                        double rate = placement.sequence_rate;
                        bool loop = placement.sequence_loop;
                        if (!placement.sequence_extra_placements.empty()) {
                            const auto& extra = placement.sequence_extra_placements.front();
                            check(
                                placement.sequence_extra_placements.size() == 1 &&
                                    extra.start_seconds == 4.5 && extra.rate == .5 && !extra.loop,
                                "Repeated typed controller placements differ through matching SDK");
                            if (admitted.time >= 4.5) {
                                rate = .5;
                                loop = false;
                                local = std::min(6., (admitted.time - 4.5) * .5);
                            }
                        }
                        check(client.entity_reference(child.id(), child_ref) == 1 &&
                                  placement.sequence.scene.str() == ref.scene_uuid &&
                                  placement.sequence_start_seconds == 1 &&
                                  placement.duration == 6 && placement.playback_speed == 2 &&
                                  !placement.play_on_start && placement.loop &&
                                  client.animation_playback(child_ref.scene_uuid,
                                                            child_ref.entity_uuid, status) == 1 &&
                                  status.duration == 6 && bool(status.loop) == loop &&
                                  status.speed == config.playback_speed * rate &&
                                  (placement.sequence_rate == 1 || placement.sequence_rate == 2) &&
                                  std::abs(status.time - local) < 1e-9,
                              "Nested typed values/derived status differ through matching SDK");
                        check(client.animation_control(child_ref.scene_uuid, child_ref.entity_uuid,
                                                       FORGE_SDK_ANIMATION_PAUSE) == 0 &&
                                  client.animation_seek(child_ref.scene_uuid, child_ref.entity_uuid,
                                                        .25) == 0,
                              "C++ independently controlled a bound sequence");
                        const auto copied = placement;
                        child.set(copied);
                        controllers.emplace(child_ref.entity_uuid, status);
                        ++nested;
                    });
                    unsigned tracks = 0, pairs = 0;
                    current.each([&](flecs::entity child, const forge::TransformAnimator& player) {
                        if (!controllers.contains(player.sequence.entity.str()))
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
                        if (!player.sequence_extra_clips.empty()) {
                            check_collection(player, status, 1);
                            const auto copied = player;
                            child.set(copied);
                            ++pairs;
                        } else if (player.sequence_next_clip.id) {
                            check(player.clip.id && player.sequence_next_start_seconds == 1 &&
                                      std::abs(status.duration - 3) < 1e-9 &&
                                      player.sequence_next_clip.id.str() ==
                                          status.blend_clip_uuid &&
                                      std::abs(status.blend_weight -
                                               std::clamp(status.time - 1., 0., 1.)) < 1e-9,
                                  "Sequence pair values/span differ through matching SDK");
                            ++pairs;
                        }
                        ++tracks;
                    });
                    unsigned skeletal_tracks = 0;
                    current.each([&](flecs::entity child, const forge::Animator& player) {
                        if (!controllers.contains(player.sequence.entity.str()))
                            return;
                        ForgeSdkEntityRefV1 child_ref{};
                        ForgeSdkAnimationPlaybackV1 status{};
                        check(player.sequence.scene.str() == ref.scene_uuid &&
                                  player.sequence_start_seconds == 0 &&
                                  player.sequence_next_start_seconds == .5 &&
                                  player.sequence_next_clip.id == player.clip.id &&
                                  !player.play_on_start && player.loop &&
                                  player.playback_speed == 0 &&
                                  client.entity_reference(child.id(), child_ref) == 1 &&
                                  client.animation_playback(child_ref.scene_uuid,
                                                            child_ref.entity_uuid, status) == 1 &&
                                  status.speed ==
                                      controllers.at(player.sequence.entity.str()).speed &&
                                  !status.loop,
                              "Skeletal sequence typed layout, clock or pair differs through SDK");
                        if (const auto* properties = child.try_get<forge::TransformAnimator>())
                            check(
                                properties->clip.id.str() == status.clip_uuid &&
                                    std::abs(status.time -
                                             std::clamp(
                                                 controllers.at(player.sequence.entity.str()).time -
                                                     properties->sequence_start_seconds,
                                                 0., status.duration)) < 1e-9,
                                "Mixed actor did not preserve transform-first SDK status");
                        else if (!player.sequence_extra_clips.empty()) {
                            check_collection(player, status, .5);
                            check(std::abs(
                                      status.time -
                                      std::clamp(controllers.at(player.sequence.entity.str()).time,
                                                 0., 4.5)) < 1e-9,
                                  "Eight-clip skeletal clock differs through matching SDK");
                        } else
                            check(
                                player.clip.id.str() == status.clip_uuid &&
                                    player.sequence_next_clip.id.str() == status.blend_clip_uuid &&
                                    status.duration == 1.5 &&
                                    std::abs(status.time -
                                             std::clamp(
                                                 controllers.at(player.sequence.entity.str()).time,
                                                 0., 1.5)) < 1e-9 &&
                                    std::abs(status.blend_weight -
                                             std::clamp((status.time - .5) * 2, 0., 1.)) < 1e-9,
                                "Skeletal sequence span or phase differs through SDK");
                        check(client.animation_control(child_ref.scene_uuid, child_ref.entity_uuid,
                                                       FORGE_SDK_ANIMATION_PAUSE) == 0 &&
                                  client.animation_seek(child_ref.scene_uuid, child_ref.entity_uuid,
                                                        .25) == 0,
                              "C++ independently controlled a bound skeletal track");
                        const auto copied = player;
                        child.set(copied);
                        ++skeletal_tracks;
                    });
                    unsigned shots = 0;
                    current.each([&](flecs::entity actor, const forge::CameraShot& shot) {
                        if (!controllers.contains(shot.sequence.entity.str()))
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
                        check(found && shot.enabled && shot.start_seconds < shot.end_seconds &&
                                  shot.blend_in_seconds >= 0 &&
                                  shot.blend_in_seconds <= shot.end_seconds - shot.start_seconds,
                              "Camera Shot typed data/reference unavailable through matching SDK");
                        if (shot.blend_in_seconds > 0) {
                            check(shot.blend_in_seconds == .75,
                                  "Matching SDK Camera Shot blend layout differs");
                            auto next = shot;
                            next.blend_in_seconds = 1;
                            actor.set(next);
                        }
                        ++shots;
                    });
                    unsigned cues = 0;
                    current.each([&](const forge::AudioCue& cue) {
                        if (!controllers.contains(cue.sequence.entity.str()))
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
                    check((cues == 0 && shots == 0 && tracks <= 1 && skeletal_tracks == 1) ||
                              (cues == 0 && shots == 0 && tracks == 3) ||
                              (cues == 0 && shots == 0 && tracks == 1 && pairs == 1) ||
                              (cues == 0 && shots == 3 && tracks == 1) ||
                              (cues == 3 && shots == 2 && tracks == 1 && nested <= 1),
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
            .each([host](flecs::entity entity, const forge::AnimationSequence& config,
                         SequenceProbe& probe) {
                if (!probe.ticks)
                    return;
                const forge::sdk::Client client(host);
                ForgeSdkEntityRefV1 ref{};
                ForgeSdkAnimationPlaybackV1 status{};
                check(client.entity_reference(entity.id(), ref) == 1 &&
                          client.animation_playback(ref.scene_uuid, ref.entity_uuid, status) == 1,
                      "C++ sequence observation unavailable");
                check(status.clip_uuid[0] == 0 && status.duration == config.duration,
                      "Controller status fabricated a clip identity");
                check(client.animation_seek(ref.scene_uuid, ref.entity_uuid, 1) == 0,
                      "Sequence seek admitted after Animation phase");
                probe.time = status.time;
                probe.blend = 0;
                entity.world().each([&](const forge::CameraShot& shot) {
                    if (shot.sequence.entity.str() == ref.entity_uuid)
                        probe.blend = std::max(probe.blend, shot.blend_in_seconds);
                });
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
