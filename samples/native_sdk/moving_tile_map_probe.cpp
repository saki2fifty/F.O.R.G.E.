// Original FORGE integration consumer; public installed SDK only.
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <flecs.h>
#include <forge/native_sdk_identity.h>
#include <forge/physics_components.hpp>
#include <forge/sdk_client.hpp>
#include <map>
#include <memory>
#include <stdexcept>

namespace {
void require(bool value, const char* text) {
    if (!value)
        throw std::runtime_error(text);
}
struct Observation {
    unsigned ticks = 0, begins = 0, ends = 0;
    std::uint64_t epoch = 0;
};
int32_t FORGE_SDK_CALL schema(const ForgeSdkWorldV1* host, char*, uint32_t) try {
    flecs::world world(host->world);
    world.component<forge::TileMapCollider2D>("forge.tile_map_collider_2d");
    world.component<forge::PhysicsBody2D>("forge.physics_body_2d");
    return 1;
} catch (...) {
    return 0;
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) try {
    auto observations = std::make_shared<std::map<std::uint64_t, Observation>>();
    flecs::world world(host->world);
    world.system<const forge::TileMapCollider2D, const forge::PhysicsBody2D>()
        .kind(host->fixed_phase)
        .each([host, observations](flecs::entity entity, const forge::TileMapCollider2D&,
                                   const forge::PhysicsBody2D& body) {
            auto& state = (*observations)[entity.id()];
            ++state.ticks;
            if (body.motion == 1) {
                forge::sdk::Client client(host);
                ForgeSdkEntityRefV1 ref{};
                const double target[]{state.ticks * .01, 0};
                require(client.entity_reference(entity.id(), ref) == 1 &&
                            client.move_2d(ref.scene_uuid, ref.entity_uuid, target, 0, true),
                        "Moving Map SDK could not queue its Kinematic target");
            }
        })
        .add(host->fixed_tag);
    world.system<const forge::TileMapCollider2D, const forge::PhysicsBody2D>()
        .kind(host->post_physics_phase)
        .each([host, observations](flecs::entity entity, const forge::TileMapCollider2D&,
                                   const forge::PhysicsBody2D& body) {
            forge::sdk::Client client(host);
            auto& state = observations->at(entity.id());
            ForgeSdkEntityRefV1 ref{};
            ForgeSdkPhysicsBodyState2DV1 native{};
            require(client.entity_reference(entity.id(), ref) == 1 &&
                        client.body_state_2d(ref.scene_uuid, ref.entity_uuid, native) == 1,
                    "Moving Map SDK lost its body state");
            if (!state.epoch)
                state.epoch = native.epoch;
            const double offset = body.motion == 1 ? state.ticks * .01 : 0;
            require(native.epoch == state.epoch && native.motion == body.motion &&
                        std::abs(native.position[0] - offset) < 1e-5 &&
                        std::abs(native.position[1]) < 1e-5,
                    "Moving Map SDK pose/epoch does not match the admitted target");
            const std::array<double, 2> origin{-5.5 + offset, 2}, delta{0, -8};
            ForgeSdkPhysicsHit2DV1 hit{};
            require(client.raycast_2d(origin.data(), delta.data(), UINT32_MAX, true, hit) == 1 &&
                        std::strcmp(hit.entity, ref.entity_uuid) == 0 &&
                        std::abs(hit.fraction - .625) < 1e-5,
                    "Moving Map SDK ray lost compound geometry");
            require(client.raycast_2d(origin.data(), delta.data(), UINT32_MAX, false, hit) ==
                        (body.sensor ? 0 : 1),
                    "Moving Map SDK ray sensor inclusion differs");
            ForgeSdkOverlap2DV1 overlap{};
            overlap.shape = FORGE_SDK_PHYSICS_2D_CIRCLE;
            overlap.layer_mask = UINT32_MAX;
            overlap.include_sensors = 1;
            overlap.dimensions[0] = .1;
            overlap.origin[0] = origin[0];
            overlap.origin[1] = -3.5;
            std::array<ForgeSdkEntityRefV1, 4> found{};
            require(client.overlap_shape_2d(overlap, found.data(), found.size()) == 1 &&
                        std::strcmp(found[0].entity_uuid, ref.entity_uuid) == 0,
                    "Moving Map SDK overlap duplicated or lost rectangles");
            ForgeSdkSweep2DV1 sweep{};
            sweep.shape = FORGE_SDK_PHYSICS_2D_CIRCLE;
            sweep.layer_mask = UINT32_MAX;
            sweep.include_sensors = 1;
            sweep.dimensions[0] = .1;
            sweep.origin[0] = origin[0];
            sweep.origin[1] = origin[1];
            sweep.displacement[1] = delta[1];
            require(client.shape_cast_2d(sweep, hit) == 1 &&
                        std::strcmp(hit.entity, ref.entity_uuid) == 0,
                    "Moving Map SDK sweep lost compound geometry");
            const std::array excluded{ref};
            require(client.overlap_shape_2d_excluding(overlap, excluded, found.data(),
                                                      found.size()) == 0 &&
                        client.shape_cast_2d_excluding(sweep, excluded, hit) == 0,
                    "Moving Map SDK exclusion retained a Map rectangle");
            const auto count = client.contact_count_2d();
            require(count >= 0, "Moving Map SDK contact count rejected");
            for (int i = 0; i < count; ++i) {
                ForgeSdkPhysicsContact2DV1 event{};
                require(client.contact_event_2d(unsigned(i), event) == 1,
                        "Moving Map SDK contact slot rejected");
                if (!event.sensor || std::strcmp(event.first_entity_uuid, ref.entity_uuid))
                    continue;
                require(body.sensor && event.first_epoch == state.epoch && event.first_available &&
                            event.second_available &&
                            std::strcmp(event.first_scene_uuid, ref.scene_uuid) == 0,
                        "Moving Map SDK sensor event has wrong flag/scope/epoch");
                event.begin ? ++state.begins : ++state.ends;
            }
            if (state.ticks == 1 || state.ticks == 150) {
                if (state.ticks == 150 && body.sensor)
                    require(state.begins && state.ends,
                            "Moving Map SDK never observed both trigger transitions");
                char message[160];
                std::snprintf(message, sizeof(message), "Map profile SDK passed %u %u %u %u %u %u",
                              state.ticks, body.motion, unsigned(body.sensor),
                              unsigned(body.one_way), state.begins, state.ends);
                require(host->diagnostic(host->context, 1, message) == 1,
                        "Moving Map SDK diagnostic rejected");
            }
        })
        .add(host->fixed_tag);
    return 1;
} catch (const std::exception& failure) {
    if (capacity)
        std::snprintf(error, capacity, "%s", failure.what());
    return 0;
}
const char* dependencies[]{"forge.physics", "forge.resources"};
const ForgeNativeSdkV1 api{sizeof(ForgeNativeSdkV1),
                           FORGE_NATIVE_SDK_ABI,
                           FORGE_NATIVE_SDK_FINGERPRINT,
                           "project.moving_tile_map_probe",
                           "1",
                           dependencies,
                           2,
                           FORGE_SDK_RUNTIME,
                           FORGE_SDK_RUNTIME,
                           FORGE_SDK_PHYSICS | FORGE_SDK_RESOURCES,
                           FORGE_SDK_PHYSICS | FORGE_SDK_RESOURCES | FORGE_SDK_DIAGNOSTICS,
                           &ecs_init,
                           &ecs_os_api,
                           schema,
                           start,
                           nullptr};
} // namespace
extern "C" FORGE_SDK_EXPORT const ForgeNativeSdkV1* FORGE_SDK_CALL forge_native_sdk_v1() {
    return &api;
}
