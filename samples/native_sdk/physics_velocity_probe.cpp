// Original exact-SDK velocity boundary fixture; no native solver linkage.
#include <cmath>
#include <cstdio>
#include <flecs.h>
#include <forge/native_sdk.h>
#include <forge/native_sdk_identity.h>
#include <forge/physics_components.hpp>
#include <limits>
#include <stdexcept>
#include <thread>
namespace {
ForgeSdkEntityRefV1 reference(const ForgeSdkWorldV1* host, flecs::entity entity) {
    ForgeSdkEntityRefV1 ref{};
    ref.size = sizeof(ref);
    if (host->entity_reference(host->context, entity.id(), &ref) != 1)
        throw std::runtime_error("Velocity SDK target identity unavailable");
    return ref;
}
int32_t FORGE_SDK_CALL schemas(const ForgeSdkWorldV1* host, char*, uint32_t) {
    flecs::world(host->world).component<forge::PhysicsBody>("forge.physics_body");
    return 1;
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char*, uint32_t) {
    flecs::world world(host->world);
    world.system<const forge::PhysicsBody>()
        .kind(host->fixed_phase)
        .each([host](flecs::entity entity, const forge::PhysicsBody&) {
            const auto ref = reference(host, entity);
            const auto set = [&](uint32_t channel, const double* vector) {
                return host->physics_set_velocity(host->context, ref.scene_uuid, ref.entity_uuid,
                                                  channel, vector);
            };
            const double invalid[] = {std::numeric_limits<double>::infinity(), 0, 0};
            const double huge[] = {1000001, 0, 0};
            const double nan[] = {0, std::numeric_limits<double>::quiet_NaN(), 0};
            const double zero[] = {0, 0, 0};
            for (const double* vector : {invalid, huge, nan, static_cast<const double*>(nullptr)})
                for (uint32_t channel :
                     {FORGE_SDK_PHYSICS_LINEAR_VELOCITY, FORGE_SDK_PHYSICS_ANGULAR_VELOCITY})
                    if (set(channel, vector) != 0)
                        throw std::runtime_error("SDK accepted invalid velocity vector");
            if (set(2, zero) != 0 || set(UINT32_MAX, zero) != 0 ||
                host->physics_set_velocity(host->context, "invalid", ref.entity_uuid, 0, zero) != 0)
                throw std::runtime_error("SDK accepted invalid velocity channel or identity");
            int foreign_linear = -1, foreign_angular = -1;
            std::thread foreign([&] {
                foreign_linear = set(FORGE_SDK_PHYSICS_LINEAR_VELOCITY, zero);
                foreign_angular = set(FORGE_SDK_PHYSICS_ANGULAR_VELOCITY, zero);
            });
            foreign.join();
            if (foreign_linear != 0 || foreign_angular != 0)
                throw std::runtime_error("SDK accepted foreign-thread velocity command");
            ForgeSdkPhysicsBodyStateV1 before{};
            before.size = sizeof(before);
            const auto availability =
                host->physics_body_state(host->context, ref.scene_uuid, ref.entity_uuid, &before);
            double linear[] = {3, 0, 0}, angular[] = {0, 0, 2};
            const double impulse[] = {4, 0, 0}, angular_impulse[] = {0, 0, 1};
            if (set(FORGE_SDK_PHYSICS_LINEAR_VELOCITY, linear) != 1 ||
                set(FORGE_SDK_PHYSICS_ANGULAR_VELOCITY, angular) != 1 ||
                host->physics_apply(host->context, ref.scene_uuid, ref.entity_uuid,
                                    FORGE_SDK_PHYSICS_IMPULSE, impulse, nullptr) != 1 ||
                host->physics_apply(host->context, ref.scene_uuid, ref.entity_uuid,
                                    FORGE_SDK_PHYSICS_ANGULAR_IMPULSE, angular_impulse,
                                    nullptr) != 1)
                throw std::runtime_error("SDK rejected valid velocity/load commands");
            linear[0] = angular[2] = 999; // Queued intent must own a copy.
            ForgeSdkPhysicsBodyStateV1 after{};
            after.size = sizeof(after);
            if (host->physics_body_state(host->context, ref.scene_uuid, ref.entity_uuid, &after) !=
                    availability ||
                before.linear_velocity[0] != after.linear_velocity[0] ||
                before.angular_velocity[2] != after.angular_velocity[2])
                throw std::runtime_error("SDK velocity queue flushed before fixed boundary");
        })
        .add(host->fixed_tag);
    world.system<const forge::PhysicsBody>()
        .kind(host->post_physics_phase)
        .each([host](flecs::entity entity, const forge::PhysicsBody&) {
            const auto ref = reference(host, entity);
            ForgeSdkPhysicsBodyStateV1 state{};
            state.size = sizeof(state);
            // Mass2 unit cube has principal inertia1/3 kg*m^2.
            if (host->physics_body_state(host->context, ref.scene_uuid, ref.entity_uuid, &state) !=
                    1 ||
                std::abs(state.linear_velocity[0] - 5) > 1e-6 ||
                std::abs(state.angular_velocity[2] - 5) > 1e-6)
                throw std::runtime_error("SDK velocity/load order or copied input failed equation");
            const double zero[] = {0, 0, 0};
            for (uint32_t channel :
                 {FORGE_SDK_PHYSICS_LINEAR_VELOCITY, FORGE_SDK_PHYSICS_ANGULAR_VELOCITY})
                if (host->physics_set_velocity(host->context, ref.scene_uuid, ref.entity_uuid,
                                               channel, zero) != 1)
                    throw std::runtime_error("SDK rejected next-boundary zero velocity intent");
        })
        .add(host->fixed_tag);
    return 1;
}
int32_t FORGE_SDK_CALL ready(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    try {
        flecs::world(host->world)
            .each<const forge::PhysicsBody>(
                [host](flecs::entity entity, const forge::PhysicsBody&) {
                    const auto ref = reference(host, entity);
                    const double vector[] = {0, 1, 0};
                    for (uint32_t channel :
                         {FORGE_SDK_PHYSICS_LINEAR_VELOCITY, FORGE_SDK_PHYSICS_ANGULAR_VELOCITY})
                        if (host->physics_set_velocity(host->context, ref.scene_uuid,
                                                       ref.entity_uuid, channel, vector) != 0)
                            throw std::runtime_error("SDK accepted velocity outside fixed scope");
                });
        return 1;
    } catch (const std::exception& e) {
        if (capacity)
            std::snprintf(error, capacity, "%s", e.what());
        return 0;
    }
}
const char* dependencies[] = {"forge.physics"};
const ForgeNativeSdkV1 api = {sizeof(ForgeNativeSdkV1),
                              FORGE_NATIVE_SDK_ABI,
                              FORGE_NATIVE_SDK_FINGERPRINT,
                              "project.physics_velocity_probe",
                              "1",
                              dependencies,
                              1,
                              FORGE_SDK_RUNTIME,
                              FORGE_SDK_RUNTIME,
                              FORGE_SDK_PHYSICS,
                              FORGE_SDK_PHYSICS,
                              &ecs_init,
                              &ecs_os_api,
                              schemas,
                              start,
                              nullptr,
                              nullptr,
                              nullptr,
                              ready};
} // namespace
extern "C" FORGE_SDK_EXPORT const ForgeNativeSdkV1* FORGE_SDK_CALL forge_native_sdk_v1() {
    return &api;
}
