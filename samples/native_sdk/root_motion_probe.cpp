// Original public-SDK fixture and actual ordinary C++ kinematic example consumer.
#include "../projects/FeatureGallery/CodeExamples/RootMotionKinematic.hpp"
#include <cstdio>
#include <cstring>
#include <flecs.h>
#include <forge/animation_components.hpp>
#include <forge/native_sdk_identity.h>
#include <stdexcept>
#include <thread>
namespace {
struct RootProbe {
    std::uint32_t ticks = 0, applied = 0;
    double x = 0, z = 0;
    bool drive_body = false;
};
int32_t FORGE_SDK_CALL schema(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    try {
        flecs::world world(host->world);
        world.component<forge::Animator>("forge.animator");
        world.component<forge::LocalTranslation>("forge.local_translation");
        world.component<forge::LocalRotation>("forge.local_rotation");
        const auto type = world.component<RootProbe>("project.RootProbe")
                              .member<std::uint32_t>("ticks")
                              .member<std::uint32_t>("applied")
                              .member<double>("x")
                              .member<double>("z")
                              .member<bool>("drive_body");
        return host->authoring_type(host->context, type, "project.root_probe", 1,
                                    R"({"ticks":0,"applied":0,"x":0,"z":0,"drive_body":false})",
                                    "Animation", error, capacity);
    } catch (const std::exception& e) {
        if (capacity)
            std::snprintf(error, capacity, "%s", e.what());
        return 0;
    }
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char*, uint32_t) {
    flecs::world world(host->world);
    world.system<const forge::Animator, RootProbe>("project.RootBeforeAnimation")
        .kind(host->fixed_phase)
        .each([host](flecs::entity entity, const forge::Animator&, RootProbe&) {
            ForgeSdkEntityRefV1 ref{};
            const forge::sdk::Client client(host);
            if (client.entity_reference(entity.id(), ref) != 1)
                throw std::runtime_error("Root entity identity unavailable");
            ForgeSdkAnimationRootMotionV1 delta{};
            if (client.animation_root_motion(ref.scene_uuid, ref.entity_uuid, delta) != 0 ||
                delta.tick)
                throw std::runtime_error("Gameplay read a stale root motion delta");
        })
        .add(host->fixed_tag);
    world
        .system<const forge::Animator, const forge::LocalTranslation, const forge::LocalRotation,
                RootProbe>("project.RootConsumer")
        .kind(host->post_animation_phase)
        .each([host](flecs::entity entity, const forge::Animator&,
                     const forge::LocalTranslation& position, const forge::LocalRotation& rotation,
                     RootProbe& probe) {
            const forge::sdk::Client client(host);
            ForgeSdkEntityRefV1 ref{};
            if (client.entity_reference(entity.id(), ref) != 1)
                throw std::runtime_error("Root consumer identity unavailable");
            ForgeSdkAnimationRootMotionV1 delta{}, again{};
            if (client.animation_root_motion(ref.scene_uuid, ref.entity_uuid, delta) != 1 ||
                client.animation_root_motion(ref.scene_uuid, ref.entity_uuid, again) != 1 ||
                delta.tick != again.tick || delta.translation[2] != again.translation[2])
                throw std::runtime_error(
                    "Current root motion unavailable or consumed by first read");
            ForgeSdkAnimationRootMotionV1 invalid{};
            if (host->animation_root_motion(host->context, ref.scene_uuid, ref.entity_uuid,
                                            &invalid) != -1)
                throw std::runtime_error("Root SDK accepted invalid size");
            if (client.animation_root_motion("invalid", ref.entity_uuid, invalid) != -1)
                throw std::runtime_error("Root SDK accepted invalid identity");
            int foreign = 9;
            std::thread thread([&] {
                foreign = client.animation_root_motion(ref.scene_uuid, ref.entity_uuid, invalid);
            });
            thread.join();
            if (foreign != 0)
                throw std::runtime_error("Root SDK accepted foreign-thread read");
            ++probe.ticks;
            probe.applied = delta.applied;
            probe.x += delta.translation[0];
            probe.z += delta.translation[2];
            if (probe.drive_body && !forge_root_motion_kinematic(
                                        host, ref.scene_uuid, ref.entity_uuid, position, rotation))
                throw std::runtime_error(
                    "Root delta did not queue the kinematic target before Physics");
        })
        .add(host->fixed_tag);
    return 1;
}
const char* dependencies[] = {"forge.animation", "forge.input", "forge.physics"};
const ForgeNativeSdkV1 api = {sizeof(ForgeNativeSdkV1),
                              FORGE_NATIVE_SDK_ABI,
                              FORGE_NATIVE_SDK_FINGERPRINT,
                              "project.root_motion_probe",
                              "1",
                              dependencies,
                              3,
                              FORGE_SDK_AUTHORING | FORGE_SDK_RUNTIME | FORGE_SDK_VALIDATION,
                              FORGE_SDK_RUNTIME,
                              FORGE_SDK_ANIMATION | FORGE_SDK_PHYSICS,
                              FORGE_SDK_ANIMATION | FORGE_SDK_PHYSICS,
                              &ecs_init,
                              &ecs_os_api,
                              schema,
                              start,
                              nullptr};
} // namespace
extern "C" FORGE_SDK_EXPORT const ForgeNativeSdkV1* FORGE_SDK_CALL forge_native_sdk_v1() {
    return &api;
}
