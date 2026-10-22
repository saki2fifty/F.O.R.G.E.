#pragma once
#include <cstdio>
#include <exception>
#include <flecs.h>
#include <forge/native_sdk.h>

// Game policy only. Character Controller 2D owns movement and collision.
struct CharacterMovement2D {
    double speed = 4, jump_speed = 6, gravity = 9.81, terminal_speed = 30;
    bool grounded = false;
};
inline int32_t forge_register_component_CharacterMovement2D(const ForgeSdkWorldV1* host,
                                                            char* error, uint32_t capacity) {
    try {
        flecs::world world(host->world);
        const auto type = world.component<CharacterMovement2D>("gameplay.CharacterMovement2D")
                              .member<double>("speed")
                              .member<double>("jump_speed")
                              .member<double>("gravity")
                              .member<double>("terminal_speed")
                              .member<bool>("grounded");
        ecs_doc_set_name(world, type, "Character Movement 2D");
        return host->authoring_type(
            host->context, type, "project.character_movement_2d", 1,
            R"({"speed":4,"jump_speed":6,"gravity":9.81,"terminal_speed":30,"grounded":false})",
            "Physics example", error, capacity);
    } catch (const std::exception& failure) {
        if (capacity)
            std::snprintf(error, capacity, "%s", failure.what());
        return 0;
    }
}
