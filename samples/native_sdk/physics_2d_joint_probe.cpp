// Original FORGE typed joint/helper fixture; no vendor physics headers or linkage.
#include "Physics2DJoints.hpp"
#include <cmath>
#include <cstdio>
#include <forge/native_sdk_identity.h>
#include <forge/sdk_client.hpp>
#include <numbers>

namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
int32_t FORGE_SDK_CALL schema(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    try {
        flecs::world(host->world).component<forge::PhysicsJoint2D>("forge.physics_joint_2d");
        return 1;
    } catch (const std::exception& failure) {
        if (capacity)
            std::snprintf(error, capacity, "%s", failure.what());
        return 0;
    }
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    try {
        flecs::world world(host->world);
        world.system<const forge::PhysicsJoint2D>()
            .kind(host->fixed_phase)
            .each([host](flecs::entity entity, const forge::PhysicsJoint2D& joint) {
                if (!joint.enabled)
                    return;
                forge::sdk::Client client(host);
                ForgeSdkPhysicsBodyState2DV1 body{};
                body.size = sizeof(body);
                const auto scene = joint.body_b.scene.str(), id = joint.body_b.entity.str();
                require(client.body_state_2d(scene.c_str(), id.c_str(), body) == 1 && body.epoch,
                        "Joint SDK body reference is unavailable");
                require(body.motion == 2 && body.mass == 2 && std::isfinite(body.angle),
                        "Joint SDK returned invalid copied body units/state");
                if (joint.kind == 0 && joint.spring_frequency == 0)
                    forge_gallery_spring_joint_2d(entity, 2, 2, 1);
                else if (joint.kind == 1) {
                    if (!joint.motor_enabled)
                        forge_gallery_drive_joint_2d(entity, 90, 10);
                    else if (joint.hinge_motor_speed > 0 &&
                             body.angle >= joint.max_angle * std::numbers::pi / 180 - .015)
                        forge_gallery_drive_joint_2d(entity, -90, 10);
                } else if (joint.kind == 2) {
                    ForgeSdkPhysicsBodyState2DV1 anchor{};
                    anchor.size = sizeof(anchor);
                    const auto a_scene = joint.body_a.scene.str(), a_id = joint.body_a.entity.str();
                    require(client.body_state_2d(a_scene.c_str(), a_id.c_str(), anchor) == 1,
                            "Joint SDK anchor reference is unavailable");
                    if (!joint.motor_enabled)
                        forge_gallery_drive_joint_2d(entity, 1, 100);
                    else if (joint.slider_motor_speed > 0 &&
                             body.position[0] - anchor.position[0] >= joint.max_travel - .015)
                        forge_gallery_drive_joint_2d(entity, -1, 100);
                }
                char message[96];
                std::snprintf(message, sizeof(message), "Joint SDK kind %u controls passed",
                              joint.kind);
                require(host->diagnostic(host->context, 1, message) == 1,
                        "Joint SDK scoped diagnostic failed");
            })
            .add(host->fixed_tag);
        return 1;
    } catch (const std::exception& failure) {
        if (capacity)
            std::snprintf(error, capacity, "%s", failure.what());
        return 0;
    }
}
const char* dependencies[] = {"forge.physics"};
const ForgeNativeSdkV1 api = {sizeof(ForgeNativeSdkV1),
                              FORGE_NATIVE_SDK_ABI,
                              FORGE_NATIVE_SDK_FINGERPRINT,
                              "project.physics_2d_joint_probe",
                              "1",
                              dependencies,
                              1,
                              FORGE_SDK_RUNTIME,
                              FORGE_SDK_RUNTIME,
                              FORGE_SDK_PHYSICS,
                              FORGE_SDK_PHYSICS | FORGE_SDK_DIAGNOSTICS,
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
