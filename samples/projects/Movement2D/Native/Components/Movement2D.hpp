#pragma once
#include <cstdint>
#include <cstdio>
#include <exception>
#include <flecs.h>
#include <forge/native_sdk.h>

// Original example policy, not a native CharacterController. Physics Body 2D
// remains the sole pose writer. Grounded is a copied runtime observation.
struct Movement2D {
    double speed = 4, jump_speed = 6, ground_probe = .12;
    std::uint32_t floor_mask = UINT32_MAX;
    bool grounded = false;
};
inline int32_t forge_register_component_Movement2D(const ForgeSdkWorldV1* host, char* error,
                                                   uint32_t capacity) {
    try {
        flecs::world world(host->world);
        const auto type = world.component<Movement2D>("gameplay.Movement2D")
                              .member<double>("speed")
                              .member<double>("jump_speed")
                              .member<double>("ground_probe")
                              .member<std::uint32_t>("floor_mask")
                              .member<bool>("grounded");
        ecs_doc_set_name(world, type, "2D Movement");
        return host->authoring_type(
            host->context, type, "project.movement_2d", 1,
            R"({"speed":4,"jump_speed":6,"ground_probe":0.12,"floor_mask":4294967295,"grounded":false})",
            "Physics example", error, capacity);
    } catch (const std::exception& e) {
        if (capacity)
            std::snprintf(error, capacity, "%s", e.what());
        return 0;
    }
}
