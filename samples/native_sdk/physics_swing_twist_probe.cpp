// Original typed swing/twist controls and copied observations; no native solver headers.
#include <cmath>
#include <cstdio>
#include <flecs.h>
#include <forge/native_sdk.h>
#include <forge/native_sdk_identity.h>
#include <forge/physics_components.hpp>
#include <stdexcept>
namespace {
struct SwingProbe {
    std::uint64_t ticks = 0;
    std::uint32_t axis = 0;
    float speed = 2;
};
int32_t FORGE_SDK_CALL schemas(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    try {
        flecs::world world(host->world);
        world.component<forge::PhysicsJoint>("forge.physics_joint");
        const auto type = world.component<SwingProbe>("project.SwingProbe")
                              .member<std::uint64_t>("ticks")
                              .member<std::uint32_t>("axis")
                              .member<float>("speed");
        return host->authoring_type(host->context, type, "project.swing_probe", 1,
                                    R"({"ticks":0,"axis":0,"speed":2})", "Physics", error,
                                    capacity);
    } catch (const std::exception& e) {
        if (capacity)
            std::snprintf(error, capacity, "%s", e.what());
        return 0;
    }
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char*, uint32_t) {
    flecs::world world(host->world);
    world.system<forge::PhysicsJoint, SwingProbe>("project.SwingControl")
        .kind(host->fixed_phase)
        .each([host](forge::PhysicsJoint& joint, SwingProbe& probe) {
            if (joint.kind != 5 || probe.axis > 2 || !std::isfinite(probe.speed))
                throw std::runtime_error("Invalid swing/twist probe configuration");
            joint.swing_normal_angle = 20;
            joint.swing_plane_angle = 10;
            if (++probe.ticks == 1) {
                const auto scene = joint.body_b.scene.str(), entity = joint.body_b.entity.str();
                double velocity[3]{};
                velocity[probe.axis] = probe.speed;
                if (host->physics_set_velocity(host->context, scene.c_str(), entity.c_str(),
                                               FORGE_SDK_PHYSICS_ANGULAR_VELOCITY, velocity) != 1)
                    throw std::runtime_error("Swing/twist initial angular velocity rejected");
            }
        })
        .add(host->fixed_tag);
    world.system<const forge::PhysicsJoint, const SwingProbe>("project.SwingObservation")
        .kind(host->post_physics_phase)
        .each([host](const forge::PhysicsJoint& joint, const SwingProbe& probe) {
            ForgeSdkPhysicsBodyStateV1 state{};
            state.size = sizeof(state);
            const auto scene = joint.body_b.scene.str(), entity = joint.body_b.entity.str();
            if (host->physics_body_state(host->context, scene.c_str(), entity.c_str(), &state) !=
                    1 ||
                probe.axis > 2 || !std::isfinite(state.angular_velocity[probe.axis]))
                throw std::runtime_error("Swing/twist probe has no copied body state");
            char text[128];
            std::snprintf(text, sizeof(text), "Swing native angular %.9f | tick %llu",
                          state.angular_velocity[probe.axis],
                          static_cast<unsigned long long>(probe.ticks));
            if (host->diagnostic(host->context, 1, text) != 1)
                throw std::runtime_error("Swing/twist diagnostic rejected");
        })
        .add(host->fixed_tag);
    return 1;
}
const char* dependencies[] = {"forge.physics"};
const ForgeNativeSdkV1 api = {sizeof(ForgeNativeSdkV1),
                              FORGE_NATIVE_SDK_ABI,
                              FORGE_NATIVE_SDK_FINGERPRINT,
                              "project.physics_swing_twist_probe",
                              "1",
                              dependencies,
                              1,
                              FORGE_SDK_AUTHORING | FORGE_SDK_RUNTIME | FORGE_SDK_VALIDATION,
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
