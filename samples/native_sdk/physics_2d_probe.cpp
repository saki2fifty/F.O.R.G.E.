// Original FORGE exact-SDK boundary fixture. No native physics linkage.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <flecs.h>
#include <forge/native_sdk_identity.h>
#include <forge/physics_components.hpp>
#include <forge/sdk_client.hpp>
#include <limits>
#include <stdexcept>
#include <thread>
namespace {
void check(bool condition, const char* text) {
    if (!condition)
        throw std::runtime_error(text);
}
ForgeSdkEntityRefV1 reference(const ForgeSdkWorldV1* host, flecs::entity entity) {
    ForgeSdkEntityRefV1 ref{};
    ref.size = sizeof(ref);
    check(host->entity_reference(host->context, entity.id(), &ref) == 1,
          "Planar SDK identity missing");
    return ref;
}
int32_t FORGE_SDK_CALL schema(const ForgeSdkWorldV1* host, char*, uint32_t) {
    flecs::world(host->world).component<forge::PhysicsBody2D>("forge.physics_body_2d");
    return 1;
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char*, uint32_t) {
    flecs::world world(host->world);
#ifdef FORGE_TEST_PLANAR_DENIED
    world.system<const forge::PhysicsBody2D>()
        .kind(host->fixed_phase)
        .each([host](flecs::entity entity, const forge::PhysicsBody2D& body) {
            if (body.motion != 2)
                return;
            const auto ref = reference(host, entity);
            const auto p = host->context;
            const auto scene = ref.scene_uuid, id = ref.entity_uuid;
            const double zero[2]{};
            ForgeSdkPhysicsLoad2DV1 load{};
            load.size = sizeof(load);
            check(host->physics_move_2d(p, scene, id, zero, 0, 0, 0) == 0 &&
                      host->physics_apply_2d(p, scene, id, &load) == 0 &&
                      host->physics_set_linear_velocity_2d(p, scene, id, zero) == 0 &&
                      host->physics_set_angular_velocity_2d(p, scene, id, 0) == 0,
                  "Planar SDK exposed commands without Physics permission");
            ForgeSdkPhysicsBodyState2DV1 state{};
            state.size = sizeof(state);
            state.epoch = 42;
            ForgeSdkPhysicsHit2DV1 hit{};
            hit.size = sizeof(hit);
            hit.fraction = 42;
            ForgeSdkPhysicsContact2DV1 event{};
            event.size = sizeof(event);
            event.tick = 42;
            double velocity[] = {42, 42};
            ForgeSdkOverlap2DV1 query{};
            query.size = sizeof(query);
            query.shape = FORGE_SDK_PHYSICS_2D_BOX;
            query.dimensions[0] = query.dimensions[1] = 1;
            query.layer_mask = UINT32_MAX;
            ForgeSdkEntityRefV1 overlap{};
            overlap.size = sizeof(overlap);
            overlap.version = 42;
            check(host->physics_body_state_2d(p, scene, id, &state) == -1 && !state.epoch &&
                      host->physics_point_velocity_2d(p, scene, id, zero, velocity) == -1 &&
                      velocity[0] == 0 && velocity[1] == 0 &&
                      host->physics_raycast_2d(p, zero, zero, UINT32_MAX, 1, &hit) == -1 &&
                      !hit.fraction &&
                      host->physics_overlap_shape_2d(p, &query, &overlap, 1) == -1 &&
                      !overlap.version && host->physics_contact_count_2d(p) == -1 &&
                      host->physics_contact_event_2d(p, 0, &event) == -1 && !event.tick,
                  "Planar SDK exposed observations without Physics permission");
            check(host->diagnostic(p, 1, "Planar SDK permission denial passed") == 1,
                  "Planar SDK denied diagnostic failed");
        })
        .add(host->fixed_tag);
#else
    world.system<const forge::PhysicsBody2D>()
        .kind(host->fixed_phase)
        .each([host](flecs::entity entity, const forge::PhysicsBody2D& body) {
            if (body.motion != 2)
                return;
            const auto ref = reference(host, entity);
            forge::sdk::Client client(host);
            ForgeSdkPhysicsBodyState2DV1 state{};
            check(client.body_state_2d(ref.scene_uuid, ref.entity_uuid, state) == 1 && state.epoch,
                  "Planar SDK state unavailable before Physics");
            check(client.contact_count_2d() == 0, "Planar SDK replayed prior-tick events");
            const double bad[] = {std::numeric_limits<double>::infinity(), 0};
            const double huge[] = {1000001, 0};
            const double velocity[] = {2, 0};
            for (const double* input : {bad, huge, static_cast<const double*>(nullptr)})
                check(!client.set_linear_velocity_2d(ref.scene_uuid, ref.entity_uuid, input),
                      "Planar SDK accepted invalid linear velocity");
            check(!client.set_angular_velocity_2d(ref.scene_uuid, ref.entity_uuid, bad[0]),
                  "Planar SDK accepted invalid angular velocity");
            check(host->physics_move_2d(host->context, ref.scene_uuid, ref.entity_uuid, velocity, 0,
                                        2, 0) == 0 &&
                      host->physics_move_2d(host->context, ref.scene_uuid, ref.entity_uuid,
                                            velocity, 0, 1, 1) == 0 &&
                      host->physics_move_2d(host->context, ref.scene_uuid, ref.entity_uuid, huge, 0,
                                            0, 0) == 0,
                  "Planar SDK accepted invalid target policy/range");
            ForgeSdkPhysicsLoad2DV1 load{};
            load.size = sizeof(load);
            load.kind = FORGE_SDK_PHYSICS_FORCE;
            load.angular = 1;
            check(host->physics_apply_2d(host->context, ref.scene_uuid, ref.entity_uuid, &load) ==
                      0,
                  "Planar SDK accepted an unused angular load");
            load.angular = 0;
            load.at_point = 2;
            check(host->physics_apply_2d(host->context, ref.scene_uuid, ref.entity_uuid, &load) ==
                      0,
                  "Planar SDK accepted invalid point flag");
            load.at_point = 0;
            load.size = 0;
            check(host->physics_apply_2d(host->context, ref.scene_uuid, ref.entity_uuid, &load) ==
                      0,
                  "Planar SDK accepted invalid load size");
            if (state.position[0] < -2.99) {
                int command = 1, read = 1;
                std::thread foreign([&] {
                    command = host->physics_set_linear_velocity_2d(host->context, ref.scene_uuid,
                                                                   ref.entity_uuid, velocity);
                    ForgeSdkPhysicsBodyState2DV1 copied{};
                    copied.size = sizeof(copied);
                    read = host->physics_body_state_2d(host->context, ref.scene_uuid,
                                                       ref.entity_uuid, &copied);
                });
                foreign.join();
                check(command == 0 && read == -1, "Planar SDK accepted a foreign thread");
            }
            if (state.position[0] > 0 && body.mass == 2) {
                auto replacement = body;
                replacement.mass = 3;
                entity.set<forge::PhysicsBody2D>(replacement);
            }
            check(client.set_linear_velocity_2d(ref.scene_uuid, ref.entity_uuid, velocity) &&
                      client.set_angular_velocity_2d(ref.scene_uuid, ref.entity_uuid, .4),
                  "Planar SDK velocity controls failed");
            for (uint32_t kind = 0; kind < 4; ++kind) {
                load = {};
                load.kind = kind;
                if (kind < FORGE_SDK_PHYSICS_TORQUE) {
                    load.linear[0] = kind == FORGE_SDK_PHYSICS_FORCE ? 6 : 2;
                    load.at_point = 1;
                    load.point[0] = state.centre_of_mass[0];
                    load.point[1] = state.centre_of_mass[1];
                } else
                    load.angular = kind == FORGE_SDK_PHYSICS_TORQUE ? .2 : .5;
                check(client.apply_2d(ref.scene_uuid, ref.entity_uuid, load),
                      "Planar SDK load rejected");
            }
        })
        .add(host->fixed_tag);
    world.system<const forge::PhysicsBody2D>()
        .kind(host->post_physics_phase)
        .each([host](flecs::iter& it, size_t row, const forge::PhysicsBody2D& body) {
            if (body.motion != 2)
                return;
            const auto ref = reference(host, it.entity(row));
            forge::sdk::Client client(host);
            ForgeSdkPhysicsBodyState2DV1 state{};
            check(client.body_state_2d(ref.scene_uuid, ref.entity_uuid, state) == 1,
                  "Planar SDK adopted state unavailable");
            const double dt = it.delta_time();
            check(std::abs(state.linear_velocity[0] - (2 + (2 + 6 * dt) / state.mass)) < 2e-5 &&
                      std::abs(state.angular_velocity -
                               (.4 + (.5 + .2 * dt) / state.rotational_inertia)) < 3e-5,
                  "Planar SDK linear/angular load units or ordering differ");
            double at[] = {state.centre_of_mass[0], state.centre_of_mass[1] + 1};
            check(client.point_velocity_2d(ref.scene_uuid, ref.entity_uuid, at, at) == 1 &&
                      std::abs(at[0] - (state.linear_velocity[0] - state.angular_velocity)) <
                          2e-5 &&
                      std::abs(at[1] - state.linear_velocity[1]) < 2e-5,
                  "Planar SDK aliased point velocity is incorrect");
            ForgeSdkPhysicsHit2DV1 hit{};
            hit.size = sizeof(hit);
            hit.position[0] = 0;
            hit.position[1] = 5;
            hit.normal[0] = 0;
            hit.normal[1] = -10;
            check(client.raycast_2d(hit.position, hit.normal, UINT32_MAX, false, hit) == 1 &&
                      hit.normal[1] > .9 && std::abs(hit.fraction - .45) < 1e-5,
                  "Planar SDK aliased ray lost solid filtering/geometry");
            const double origin[] = {0, 5}, miss[] = {0, 1};
            check(client.raycast_2d(origin, miss, UINT32_MAX, true, hit) == 0 && !hit.scene[0] &&
                      !hit.entity[0] && hit.fraction == 0 && hit.position[0] == 0,
                  "Planar SDK miss retained hit output");
            ForgeSdkOverlap2DV1 query{};
            query.shape = FORGE_SDK_PHYSICS_2D_BOX;
            query.layer_mask = UINT32_MAX;
            query.include_sensors = 1;
            query.dimensions[0] = query.dimensions[1] = 20;
            ForgeSdkEntityRefV1 hits[3]{};
            check(client.overlap_shape_2d(query, hits, 1) == -2 && hits[0].version == 0,
                  "Planar SDK silently truncated overlap results");
            check(client.overlap_shape_2d(query, hits, 3) == 3,
                  "Planar SDK overlap lost canonical bodies");
            for (unsigned i = 1; i < 3; ++i)
                check(std::strcmp(hits[i - 1].scene_uuid, hits[i].scene_uuid) < 0 ||
                          (std::strcmp(hits[i - 1].scene_uuid, hits[i].scene_uuid) == 0 &&
                           std::strcmp(hits[i - 1].entity_uuid, hits[i].entity_uuid) < 0),
                      "Planar SDK overlap references are not unique/sorted");
            const auto count = client.contact_count_2d();
            check(count >= 0 && count <= 16384, "Planar SDK contact count invalid");
            for (int32_t i = 0; i < count; ++i) {
                ForgeSdkPhysicsContact2DV1 event{}, again{};
                check(client.contact_event_2d(uint32_t(i), event) == 1 && event.sensor &&
                          client.contact_event_2d(uint32_t(i), again) == 1 &&
                          event.tick == again.tick && event.first_epoch == again.first_epoch &&
                          event.second_epoch == again.second_epoch && event.begin == again.begin,
                      "Planar SDK contact slots were consumed or changed");
                char text[192];
                std::snprintf(text, sizeof(text), "Planar SDK %s | tick %llu | retired %u",
                              event.begin ? "Begin" : "End",
                              static_cast<unsigned long long>(event.tick),
                              unsigned(!event.first_available || !event.second_available));
                check(host->diagnostic(host->context, 1, text) == 1,
                      "Planar SDK event diagnostic failed");
            }
            ForgeSdkPhysicsContact2DV1 absent{};
            absent.begin = 42;
            check(client.contact_event_2d(uint32_t(count), absent) == 0 && absent.begin == 0,
                  "Planar SDK absent slot retained output");
            check(host->diagnostic(host->context, 1,
                                   "Planar SDK controls and observations passed") == 1,
                  "Planar SDK acceptance diagnostic failed");
        })
        .add(host->fixed_tag);
#endif
    return 1;
}
int32_t FORGE_SDK_CALL ready(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    try {
        check(host->physics_contact_count_2d(host->context) == -1,
              "Planar SDK allowed a nonfixed observation");
        const double zero[2]{};
        check(host->physics_move_2d(host->context, "bad", "bad", zero, 0, 0, 0) == 0,
              "Planar SDK allowed a nonfixed movement");
        return 1;
    } catch (const std::exception& e) {
        if (capacity)
            std::snprintf(error, capacity, "%s", e.what());
        return 0;
    }
}
const char* dependencies[] = {"forge.physics"};
#ifdef FORGE_TEST_PLANAR_DENIED
const char* module_id = "project.physics_2d_denied";
constexpr uint32_t required = 0, allowed = FORGE_SDK_DIAGNOSTICS;
#else
const char* module_id = "project.physics_2d_probe";
constexpr uint32_t required = FORGE_SDK_PHYSICS,
                   allowed = FORGE_SDK_PHYSICS | FORGE_SDK_DIAGNOSTICS;
#endif
const ForgeNativeSdkV1 api = {sizeof(ForgeNativeSdkV1),
                              FORGE_NATIVE_SDK_ABI,
                              FORGE_NATIVE_SDK_FINGERPRINT,
                              module_id,
                              "1",
                              dependencies,
                              1,
                              FORGE_SDK_RUNTIME,
                              FORGE_SDK_RUNTIME,
                              required,
                              allowed,
                              &ecs_init,
                              &ecs_os_api,
                              schema,
                              start,
                              nullptr,
                              nullptr,
                              nullptr,
                              ready};
} // namespace
extern "C" FORGE_SDK_EXPORT const ForgeNativeSdkV1* FORGE_SDK_CALL forge_native_sdk_v1() {
    return &api;
}
