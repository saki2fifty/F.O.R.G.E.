// Original FORGE exact-SDK acceptance fixture; no native physics linkage.
#include <cmath>
#include <cstdio>
#include <flecs.h>
#include <forge/character_components.hpp>
#include <forge/native_sdk_identity.h>
#include <forge/sdk_client.hpp>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>

namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
int32_t FORGE_SDK_CALL schema(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    try {
        flecs::world(host->world)
            .component<forge::CharacterController2D>("forge.character_controller_2d");
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
        auto frame = std::make_shared<unsigned>(0);
        world.system<const forge::CharacterController2D>()
            .kind(host->fixed_phase)
            .each([host, frame](flecs::entity entity, const forge::CharacterController2D&) {
                ++*frame;
                forge::sdk::Client client(host);
                ForgeSdkEntityRefV1 ref{};
                ref.size = sizeof(ref);
                require(host->entity_reference(host->context, entity.id(), &ref) == 1,
                        "Character2D SDK identity unavailable");
                const auto scene = ref.scene_uuid, id = ref.entity_uuid;
                ForgeSdkCharacter2DV1 before{}, after{};
                const double zero[2]{};
#ifdef FORGE_TEST_CHARACTER_2D_DENIED
                before.size = sizeof(before);
                before.epoch = 42;
                before.position[0] = 42;
                require(!host->character_state_2d(host->context, scene, id, &before) &&
                            !before.epoch && before.position[0] == 0 &&
                            !host->character_command_2d(host->context, scene, id, 0, zero, 0) &&
                            !client.place_character_2d(scene, id, zero, true),
                        "Character2D SDK bypassed Physics permission");
                require(host->diagnostic(host->context, 1, "Character2D SDK denied") == 1,
                        "Character2D denied diagnostic unavailable");
#else
                require(client.character_2d(scene, id, before) && before.epoch,
                        "Character2D SDK state unavailable before Physics");
                if (*frame == 1) {
                    const double invalid[]{std::numeric_limits<double>::infinity(), 0};
                    const double huge[]{1000001, 0};
                    for (const auto* value : {invalid, huge, static_cast<const double*>(nullptr)})
                        require(!client.move_character_2d(scene, id, value) &&
                                    !client.place_character_2d(scene, id, value, false),
                                "Character2D SDK accepted invalid vector");
                    require(!host->character_command_2d(host->context, scene, id, 2, zero, 0) &&
                                !host->character_command_2d(host->context, scene, id, 0, zero, 1) &&
                                !host->character_command_2d(host->context, scene, id, 1, zero, 2),
                            "Character2D SDK accepted invalid operation/flag");
                    ForgeSdkCharacter2DV1 wrong{};
                    wrong.size = sizeof(wrong) - 1;
                    wrong.epoch = 42;
                    require(!host->character_state_2d(host->context, scene, id, &wrong) &&
                                wrong.epoch == 42 &&
                                !host->character_state_2d(host->context, scene, id, nullptr),
                            "Character2D SDK wrote an invalid-size output");
                    wrong.size = sizeof(wrong);
                    require(!host->character_state_2d(host->context, "bad", id, &wrong) &&
                                !wrong.epoch && !client.move_character_2d("bad", id, zero),
                            "Character2D SDK retained invalid-target output");
                    const double zero3[3]{};
                    require(!client.character_command(scene, id, 0, zero3),
                            "Character2D SDK accepted a3D control");
                    bool rejected = false;
                    std::thread foreign([&] {
                        ForgeSdkCharacter2DV1 state{};
                        state.size = sizeof(state);
                        state.epoch = 42;
                        rejected = !host->character_state_2d(host->context, scene, id, &state) &&
                                   !state.epoch && !client.move_character_2d(scene, id, zero);
                    });
                    foreign.join();
                    require(rejected, "Character2D SDK accepted foreign-thread access");
                }
                double velocity[]{3, -1};
                if (*frame == 41) {
                    const double blocked[]{2.5, 0};
                    require(client.place_character_2d(scene, id, blocked, true),
                            "Character2D SDK rejected queued blocked placement");
                } else if (*frame == 42) {
                    const double first[]{2, 0}, feet[]{-1, 2}, last[]{-3, 0};
                    require(client.move_character_2d(scene, id, first) &&
                                client.place_character_2d(scene, id, feet, true) &&
                                client.move_character_2d(scene, id, last),
                            "Character2D SDK rejected FIFO commands");
                } else if (*frame == 43) {
                    require(client.place_character_2d(scene, id, zero, true),
                            "Character2D SDK rejected clear feet placement");
                } else {
                    if (*frame >= 44 && *frame <= 53) {
                        velocity[0] = 0;
                        velocity[1] = 3;
                    } else if (*frame >= 54) {
                        velocity[0] = 0;
                        velocity[1] = -3;
                    }
                    require(client.move_character_2d(scene, id, velocity),
                            "Character2D SDK rejected full XY intent");
                }
                require(client.character_2d(scene, id, after) &&
                            before.position[0] == after.position[0] &&
                            before.position[1] == after.position[1] && before.epoch == after.epoch,
                        "Character2D SDK read flushed pending commands");
#endif
            })
            .add(host->fixed_tag);
#ifndef FORGE_TEST_CHARACTER_2D_DENIED
        world.system<const forge::CharacterController2D>()
            .kind(host->post_physics_phase)
            .each([host, frame](flecs::entity entity, const forge::CharacterController2D&) {
                ForgeSdkEntityRefV1 ref{};
                ref.size = sizeof(ref);
                require(host->entity_reference(host->context, entity.id(), &ref) == 1,
                        "Character2D SDK adopted identity unavailable");
                ForgeSdkCharacter2DV1 state{};
                require(
                    forge::sdk::Client(host).character_2d(ref.scene_uuid, ref.entity_uuid, state),
                    "Character2D SDK adopted state unavailable");
                if (*frame == 41)
                    require(state.placement_blocked && std::abs(state.position[0] - 1.705) < 1e-4,
                            "Character2D blocked placement moved the actor");
                if (*frame == 42)
                    require(!state.placement_blocked && std::abs(state.position[0] + 1.05) < 1e-5 &&
                                state.position[1] == 2 && state.velocity[0] == -3,
                            "Character2D SDK FIFO/clear placement differed");
                if (*frame <= 41 || *frame >= 65)
                    require(state.ground == 0 && state.supporting_epoch && state.support_scene[0] &&
                                state.support_entity[0] && state.ground_normal[1] > .99 &&
                                std::abs(state.ground_position[1]) < 1e-5,
                            "Character2D SDK lost copied floor support");
                if (*frame >= 44 && *frame <= 53)
                    require(state.ground == 3 && !state.supporting_epoch &&
                                !state.support_entity[0],
                            "Character2D SDK retained support during departure");
                char message[192];
                std::snprintf(message, sizeof(message), "Character2D SDK passed %u %.9f %.9f %llu",
                              *frame, state.position[0], state.position[1],
                              static_cast<unsigned long long>(state.epoch));
                require(host->diagnostic(host->context, 1, message) == 1,
                        "Character2D SDK diagnostic unavailable");
            })
            .add(host->fixed_tag);
#endif
        return 1;
    } catch (const std::exception& failure) {
        if (capacity)
            std::snprintf(error, capacity, "%s", failure.what());
        return 0;
    }
}
int32_t FORGE_SDK_CALL ready(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    try {
        ForgeSdkCharacter2DV1 state{};
        state.size = sizeof(state);
        state.epoch = 42;
        const double zero[2]{};
        require(!host->character_state_2d(host->context, "bad", "bad", &state) && !state.epoch &&
                    !host->character_command_2d(host->context, "bad", "bad", 0, zero, 0),
                "Character2D SDK accepted nonfixed access");
        return 1;
    } catch (const std::exception& failure) {
        if (capacity)
            std::snprintf(error, capacity, "%s", failure.what());
        return 0;
    }
}
const char* dependencies[]{"forge.physics"};
#ifdef FORGE_TEST_CHARACTER_2D_DENIED
constexpr auto module_id = "project.character_2d_denied";
constexpr uint32_t required = 0, allowed = FORGE_SDK_DIAGNOSTICS;
#else
constexpr auto module_id = "project.character_2d_probe";
constexpr uint32_t required = FORGE_SDK_PHYSICS,
                   allowed = FORGE_SDK_PHYSICS | FORGE_SDK_DIAGNOSTICS;
#endif
const ForgeNativeSdkV1 api{sizeof(ForgeNativeSdkV1),
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
