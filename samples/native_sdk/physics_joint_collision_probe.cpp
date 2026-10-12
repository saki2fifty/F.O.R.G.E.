// Typed joint controls and copied native contacts; no Jolt or private headers.
#include <cmath>
#include <cstdio>
#include <flecs.h>
#include <forge/native_sdk.h>
#include <forge/native_sdk_identity.h>
#include <forge/physics_components.hpp>
#include <stdexcept>
namespace {
struct CollisionCycle {
    std::uint64_t ticks = 0;
};
int32_t FORGE_SDK_CALL schemas(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    try {
        flecs::world world(host->world);
        world.component<forge::PhysicsJoint>("forge.physics_joint");
        const auto type = world.component<CollisionCycle>("project.CollisionCycle")
                              .member<std::uint64_t>("ticks");
        return host->authoring_type(host->context, type, "project.collision_cycle", 1,
                                    R"({"ticks":0})", "Physics", error, capacity);
    } catch (const std::exception& e) {
        if (capacity)
            std::snprintf(error, capacity, "%s", e.what());
        return 0;
    }
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char*, uint32_t) {
    flecs::world world(host->world);
    world.system<forge::PhysicsJoint, CollisionCycle>("project.CollisionControl")
        .kind(host->fixed_phase)
        .each([](forge::PhysicsJoint& joint, CollisionCycle& cycle) {
            ++cycle.ticks;
            joint.collide_connected = cycle.ticks >= 31 && cycle.ticks < 61;
        })
        .add(host->fixed_tag);
    world.system<const forge::PhysicsJoint, const CollisionCycle>("project.CollisionObservation")
        .kind(host->post_physics_phase)
        .each([host](const forge::PhysicsJoint& joint, const CollisionCycle& cycle) {
            ForgeSdkPhysicsBodyStateV1 state{};
            state.size = sizeof(state);
            const auto scene = joint.body_b.scene.str(), entity = joint.body_b.entity.str();
            if (host->physics_body_state(host->context, scene.c_str(), entity.c_str(), &state) !=
                    1 ||
                !std::isfinite(state.centre_of_mass[0]))
                throw std::runtime_error("Joint collision probe has no copied body state");
            const auto count = host->physics_contact_count(host->context);
            if (count < 0)
                throw std::runtime_error("Joint collision probe cannot read contacts");
            unsigned begins = 0, ends = 0;
            for (int32_t i = 0; i < count; ++i) {
                ForgeSdkPhysicsContactV1 event{};
                event.size = sizeof(event);
                if (host->physics_contact_event(host->context, uint32_t(i), &event) != 1)
                    throw std::runtime_error("Joint collision probe lost a current contact");
                if (event.begin)
                    ++begins;
                else
                    ++ends;
            }
            char text[128];
            std::snprintf(text, sizeof(text), "Joint contacts %u/%u | x %.9f | cycle %llu", begins,
                          ends, state.centre_of_mass[0],
                          static_cast<unsigned long long>(cycle.ticks));
            if (host->diagnostic(host->context, 1, text) != 1)
                throw std::runtime_error("Joint collision diagnostic rejected");
        })
        .add(host->fixed_tag);
    return 1;
}
const char* dependencies[] = {"forge.physics"};
const ForgeNativeSdkV1 api = {sizeof(ForgeNativeSdkV1),
                              FORGE_NATIVE_SDK_ABI,
                              FORGE_NATIVE_SDK_FINGERPRINT,
                              "project.physics_joint_collision_probe",
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
