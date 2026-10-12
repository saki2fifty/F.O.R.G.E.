// Original typed distance-spring controls and copied observations; public SDK only.
#include <cmath>
#include <cstdio>
#include <flecs.h>
#include <forge/native_sdk.h>
#include <forge/native_sdk_identity.h>
#include <forge/physics_components.hpp>
#include <stdexcept>
namespace {
struct SpringProbe {
    std::uint64_t ticks = 0;
    float frequency = 1, damping = .15f;
    std::uint32_t switch_tick = 45;
};
int32_t FORGE_SDK_CALL schemas(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    try {
        flecs::world world(host->world);
        world.component<forge::PhysicsJoint>("forge.physics_joint");
        const auto type = world.component<SpringProbe>("project.SpringProbe")
                              .member<std::uint64_t>("ticks")
                              .member<float>("frequency")
                              .member<float>("damping")
                              .member<std::uint32_t>("switch_tick");
        return host->authoring_type(host->context, type, "project.spring_probe", 1,
                                    R"({"ticks":0,"frequency":1,"damping":0.15,"switch_tick":45})",
                                    "Physics", error, capacity);
    } catch (const std::exception& e) {
        if (capacity)
            std::snprintf(error, capacity, "%s", e.what());
        return 0;
    }
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char*, uint32_t) {
    flecs::world world(host->world);
    world.system<forge::PhysicsJoint, SpringProbe>("project.SpringControl")
        .kind(host->fixed_phase)
        .each([](forge::PhysicsJoint& joint, SpringProbe& probe) {
            if (joint.kind != 0 || !std::isfinite(probe.frequency) || !std::isfinite(probe.damping))
                throw std::runtime_error("Invalid distance spring probe configuration");
            if (++probe.ticks == probe.switch_tick) {
                probe.frequency = 2;
                probe.damping = 1;
            }
            joint.distance_spring_frequency = probe.frequency;
            joint.distance_spring_damping = probe.damping;
        })
        .add(host->fixed_tag);
    world.system<const forge::PhysicsJoint, const SpringProbe>("project.SpringObservation")
        .kind(host->post_physics_phase)
        .each([host](const forge::PhysicsJoint& joint, const SpringProbe& probe) {
            ForgeSdkPhysicsBodyStateV1 state{};
            state.size = sizeof(state);
            const auto scene = joint.body_b.scene.str(), entity = joint.body_b.entity.str();
            if (host->physics_body_state(host->context, scene.c_str(), entity.c_str(), &state) !=
                    1 ||
                !std::isfinite(state.linear_velocity[0]))
                throw std::runtime_error("Distance spring probe has no copied body state");
            char text[128];
            std::snprintf(text, sizeof(text), "Spring native velocity %.9f | tick %llu",
                          state.linear_velocity[0], static_cast<unsigned long long>(probe.ticks));
            if (host->diagnostic(host->context, 1, text) != 1)
                throw std::runtime_error("Distance spring diagnostic rejected");
        })
        .add(host->fixed_tag);
    return 1;
}
const char* dependencies[] = {"forge.physics"};
const ForgeNativeSdkV1 api = {sizeof(ForgeNativeSdkV1),
                              FORGE_NATIVE_SDK_ABI,
                              FORGE_NATIVE_SDK_FINGERPRINT,
                              "project.physics_distance_spring_probe",
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
