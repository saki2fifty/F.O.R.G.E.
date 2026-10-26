#pragma once
#include <cstdio>
#include <exception>
#include <flecs.h>
#include <forge/native_sdk.h>

// Authored path endpoints. The system owns a separate runtime-only path clock.
struct MovingPlatform2D {
    double start_x = 1.5, start_y = 3.5, end_x = 4.5, end_y = 3.5, duration = 3;
};
inline int32_t forge_register_component_MovingPlatform2D(const ForgeSdkWorldV1* host, char* error,
                                                         uint32_t capacity) {
    try {
        flecs::world world(host->world);
        const auto type = world.component<MovingPlatform2D>("gameplay.MovingPlatform2D")
                              .member<double>("start_x")
                              .member<double>("start_y")
                              .member<double>("end_x")
                              .member<double>("end_y")
                              .member<double>("duration");
        ecs_doc_set_name(world, type, "Moving Platform 2D");
        return host->authoring_type(
            host->context, type, "project.moving_platform_2d", 1,
            R"({"start_x":1.5,"start_y":3.5,"end_x":4.5,"end_y":3.5,"duration":3})",
            "Physics example", error, capacity);
    } catch (const std::exception& failure) {
        if (capacity)
            std::snprintf(error, capacity, "%s", failure.what());
        return 0;
    }
}
