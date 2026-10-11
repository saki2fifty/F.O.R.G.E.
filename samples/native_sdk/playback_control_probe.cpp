// Matching C++ controls using the authoritative input tick, not a private timer.
#include "../projects/FeatureGallery/CodeExamples/ClipControl.hpp"
#include <cmath>
#include <cstdio>
#include <flecs.h>
#include <forge/animation_components.hpp>
#include <forge/identity.hpp>
#include <forge/native_sdk_identity.h>
#include <stdexcept>
#include <thread>
namespace {
void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F> void checked(const ForgeSdkWorldV1* host, F test) noexcept {
    try {
        test();
    } catch (const std::exception& e) {
        host->diagnostic(host->context, 3, e.what());
    }
}
int32_t FORGE_SDK_CALL schema(const ForgeSdkWorldV1*, char*, uint32_t) { return 1; }
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    try {
        check(host->animation_control(host->context, "00000000-0000-4000-8000-000000000001",
                                      "00000000-0000-4000-8000-000000000002",
                                      FORGE_SDK_ANIMATION_PLAY) == 0,
              "Control available outside fixed tick");
        flecs::world world(host->world);
        world.component<forge::Animator>("forge.animator");
        world.component<forge::StableId>("forge.stable_id");
        auto unregistered = world.entity("probe.unregistered")
                                .set<forge::StableId>({"00000000-0000-4000-8000-000000000003"});
        ForgeSdkEntityRefV1 absent{};
        absent.size = sizeof(absent);
        check(host->entity_reference(host->context, unregistered.id(), &absent) == 0 &&
                  !absent.scene_uuid[0] && !absent.entity_uuid[0],
              "Unregistered StableId fabricated canonical identity");
        unregistered.destruct();
        absent.size = sizeof(absent);
        check(host->entity_reference(host->context, unregistered.id(), &absent) == 0,
              "Stale native handle resolved");
        world.system<const forge::Animator>("probe.PlaybackControls")
            .kind(host->fixed_phase)
            .each([host](flecs::iter& it, size_t row, const forge::Animator&) {
                checked(host, [&] {
                    const forge::sdk::Client client(host);
                    ForgeSdkEntityRefV1 reference{};
                    check(client.entity_reference(it.entity(row).id(), reference) == 1 &&
                              reference.version == 1 && reference.scene_uuid[0] &&
                              reference.entity_uuid[0],
                          "Registered entity reference unavailable");
                    ForgeSdkEntityRefV1 bad{};
                    bad.size = sizeof(bad);
                    bad.scene_uuid[0] = 'x';
                    check(host->entity_reference(host->context, 0, &bad) == -1 &&
                              !bad.scene_uuid[0],
                          "Zero reference handle accepted/stale output");
                    bad.size = 1;
                    check(host->entity_reference(host->context, it.entity(row).id(), &bad) == -1,
                          "Wrong reference size accepted");
                    check(client.animation_control(reference.scene_uuid, reference.entity_uuid,
                                                   99) == -1,
                          "Unknown playback operation accepted");
                    check(host->animation_control(host->context, "not-a-uuid",
                                                  reference.entity_uuid,
                                                  FORGE_SDK_ANIMATION_PAUSE) == -1,
                          "Invalid control UUID accepted");
                    check(host->animation_control(host->context, reference.scene_uuid,
                                                  "00000000-0000-4000-8000-000000000004",
                                                  FORGE_SDK_ANIMATION_RESTART) == 0,
                          "Missing player controlled");
                    bad.size = sizeof(bad);
                    check(host->entity_reference(host->context, it.entity(row).id() | ECS_PAIR,
                                                 &bad) == -1,
                          "Role-bearing ID accepted as native entity");
                    int32_t foreign_control = 0, foreign_reference = 0;
                    const auto handle = it.entity(row).id();
                    std::thread foreign([&] {
                        ForgeSdkEntityRefV1 foreign{};
                        foreign.size = sizeof(foreign);
                        foreign_reference = host->entity_reference(host->context, handle, &foreign);
                        foreign_control = host->animation_control(
                            host->context, reference.scene_uuid, reference.entity_uuid,
                            FORGE_SDK_ANIMATION_RESTART);
                    });
                    foreign.join();
                    check(foreign_control == -1 && foreign_reference == -1,
                          "Foreign thread accessed playback/entity owner");
                    ForgeSdkActionV1 action{};
                    check(client.action("663e9451-b573-4dc7-811b-c9852c5e1301", action) &&
                              action.tick,
                          "Existing authoritative fixed action tick unavailable");
                    uint32_t operation = 0;
                    switch (action.tick) {
                    case 1:
                    case 4:
                    case 8:
                        operation = FORGE_SDK_ANIMATION_PLAY;
                        break;
                    case 2:
                    case 7:
                        operation = FORGE_SDK_ANIMATION_PAUSE;
                        break;
                    case 5:
                    case 9:
                        operation = FORGE_SDK_ANIMATION_RESTART;
                        break;
                    }
                    if (operation)
                        check(forge_control_clip(host, it.entity(row).id(), operation) == 1,
                              "Gameplay clip control rejected");
                });
            })
            .add(host->fixed_tag);
        world.system<const forge::Animator>("probe.PlaybackControlStatus")
            .kind(host->post_physics_phase)
            .each([host](flecs::iter& it, size_t row, const forge::Animator&) {
                checked(host, [&] {
                    const forge::sdk::Client client(host);
                    ForgeSdkEntityRefV1 reference{};
                    check(client.entity_reference(it.entity(row).id(), reference) == 1,
                          "PostPhysics reference unavailable");
                    ForgeSdkAnimationPlaybackV1 status{};
                    check(client.animation_playback(reference.scene_uuid, reference.entity_uuid,
                                                    status) == 1,
                          "Controlled status unavailable");
                    check(forge_control_clip(host, it.entity(row).id(),
                                             FORGE_SDK_ANIMATION_PAUSE) == 0,
                          "Late PostPhysics control accepted");
                    char text[160];
                    std::snprintf(text, sizeof(text), "control_probe: %.9f %u", status.time,
                                  status.playing);
                    host->diagnostic(host->context, 1, text);
                });
            })
            .add(host->fixed_tag);
        return 1;
    } catch (const std::exception& e) {
        if (capacity)
            std::snprintf(error, capacity, "%s", e.what());
        return 0;
    }
}
const char* dependencies[] = {"forge.animation", "forge.input"};
const ForgeNativeSdkV1 api = {sizeof(ForgeNativeSdkV1),
                              FORGE_NATIVE_SDK_ABI,
                              FORGE_NATIVE_SDK_FINGERPRINT,
                              "project.playback_controls",
                              "1",
                              dependencies,
                              2,
                              FORGE_SDK_RUNTIME,
                              FORGE_SDK_RUNTIME,
                              FORGE_SDK_ANIMATION,
                              FORGE_SDK_ANIMATION | FORGE_SDK_DIAGNOSTICS,
                              &ecs_init,
                              &ecs_os_api,
                              schema,
                              start,
                              nullptr,
                              nullptr,
                              nullptr,
                              nullptr};
} // namespace
extern "C" FORGE_SDK_EXPORT const ForgeNativeSdkV1* FORGE_SDK_CALL forge_native_sdk_v1() {
    return &api;
}
