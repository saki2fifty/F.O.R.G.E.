// Exact-SDK typed joint control and copied native observations; no Jolt dependency.
#include <cmath>
#include <cstdio>
#include <flecs.h>
#include <forge/native_sdk.h>
#include <forge/native_sdk_identity.h>
#include <forge/physics_components.hpp>
#include <stdexcept>
namespace {
int32_t FORGE_SDK_CALL schemas(const ForgeSdkWorldV1* host, char*, uint32_t) {
    flecs::world(host->world).component<forge::PhysicsJoint>("forge.physics_joint");
    return 1;
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    if (!(host->capabilities & FORGE_SDK_PHYSICS)) {
        if (capacity)
            std::snprintf(error, capacity, "Missing Physics capability");
        return 0;
    }
    flecs::world world(host->world);
    world.system<forge::PhysicsJoint>("project.MotorControl")
        .kind(host->fixed_phase)
        .each([](forge::PhysicsJoint& joint) {
            if (joint.kind == 4) {
                joint.motor_enabled = true;
                joint.slider_motor_speed = 3;
                joint.max_motor_force = 1;
            }
        })
        .add(host->fixed_tag);
    world.system<const forge::PhysicsJoint>("project.MotorObservation")
        .kind(host->post_physics_phase)
        .each([host](const forge::PhysicsJoint& joint) {
            if (joint.kind != 4)
                return;
            ForgeSdkPhysicsBodyStateV1 state{};
            state.size = sizeof(state);
            const auto scene = joint.body_b.scene.str(), entity = joint.body_b.entity.str();
            if (host->physics_body_state(host->context, scene.c_str(), entity.c_str(), &state) !=
                    1 ||
                state.motion != 2 || !std::isfinite(state.linear_velocity[0]))
                throw std::runtime_error("Compiled motor has no copied native state");
            if (std::abs(state.linear_velocity[0] - .5) < 1e-4) {
                char text[96];
                std::snprintf(text, sizeof(text), "Motor native velocity %.9f",
                              state.linear_velocity[0]);
                if (host->diagnostic(host->context, 1, text) != 1)
                    throw std::runtime_error("Compiled motor observation rejected");
            }
        })
        .add(host->fixed_tag);
    return 1;
}
const char* dependencies[] = {"forge.physics"};
const ForgeNativeSdkV1 api = {sizeof(ForgeNativeSdkV1),
                              FORGE_NATIVE_SDK_ABI,
                              FORGE_NATIVE_SDK_FINGERPRINT,
                              "project.physics_motor_probe",
                              "1",
                              dependencies,
                              1,
                              FORGE_SDK_RUNTIME,
                              FORGE_SDK_RUNTIME,
                              FORGE_SDK_PHYSICS,
                              FORGE_SDK_PHYSICS | FORGE_SDK_DIAGNOSTICS,
                              &ecs_init,
                              &ecs_os_api,
                              schemas,
                              start,
                              nullptr,
                              nullptr,
                              nullptr,
                              nullptr};
} // namespace
extern "C" FORGE_SDK_EXPORT const ForgeNativeSdkV1* FORGE_SDK_CALL forge_native_sdk_v1() {
    return &api;
}
