#pragma once
#include <cstdint>
#include <cstdio>
#include <exception>
#include <flecs.h>
#include <forge/native_sdk.h>
struct VelocityControl {
    double linear_x = 0, linear_y = 1, linear_z = 0;
    double angular_x = 0, angular_y = 0, angular_z = 1;
    double stop_after = 2, elapsed = 0;
    std::uint32_t stop_channel = 0; // 0 coast,1 linear,2 angular,3 both.
    bool launched = false, stopped = false;
};
inline int32_t forge_register_component_VelocityControl(const ForgeSdkWorldV1* host, char* error,
                                                        uint32_t capacity) {
    try {
        flecs::world world(host->world);
        const auto type = world.component<VelocityControl>("gameplay.VelocityControl")
                              .member<double>("linear_x")
                              .member<double>("linear_y")
                              .member<double>("linear_z")
                              .member<double>("angular_x")
                              .member<double>("angular_y")
                              .member<double>("angular_z")
                              .member<double>("stop_after")
                              .member<double>("elapsed")
                              .member<std::uint32_t>("stop_channel")
                              .member<bool>("launched")
                              .member<bool>("stopped");
        ecs_doc_set_name(world, type, "Velocity Control");
        return host->authoring_type(
            host->context, type, "project.velocity_control", 1,
            R"({"linear_x":0,"linear_y":1,"linear_z":0,"angular_x":0,"angular_y":0,"angular_z":1,"stop_after":2,"elapsed":0,"stop_channel":0,"launched":false,"stopped":false})",
            "Physics example", error, capacity);
    } catch (const std::exception& e) {
        if (capacity)
            std::snprintf(error, capacity, "%s", e.what());
        return 0;
    }
}
