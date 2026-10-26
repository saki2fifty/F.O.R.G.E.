#pragma once
#include <cstdio>
#include <exception>
#include <flecs.h>
#include <forge/native_sdk.h>

struct RayProbe {
    double origin_x = -5, origin_y = 0, origin_z = 0;
    double delta_x = 10, delta_y = 0, delta_z = 0;
    bool planar = false, include_sensors = true, observed = false;
    int32_t last_count = 0;
    double nearest_fraction = -1;
};
inline int32_t forge_register_component_RayProbe(const ForgeSdkWorldV1* host, char* error,
                                                 uint32_t capacity) {
    try {
        flecs::world world(host->world);
        const auto type = world.component<RayProbe>("gameplay.RayProbe")
                              .member<double>("origin_x")
                              .member<double>("origin_y")
                              .member<double>("origin_z")
                              .member<double>("delta_x")
                              .member<double>("delta_y")
                              .member<double>("delta_z")
                              .member<bool>("planar")
                              .member<bool>("include_sensors")
                              .member<bool>("observed")
                              .member<int32_t>("last_count")
                              .member<double>("nearest_fraction");
        ecs_doc_set_name(world, type, "Ray Probe");
        return host->authoring_type(
            host->context, type, "project.ray_probe", 1,
            R"({"origin_x":-5,"origin_y":0,"origin_z":0,"delta_x":10,"delta_y":0,"delta_z":0,"planar":false,"include_sensors":true,"observed":false,"last_count":0,"nearest_fraction":-1})",
            "Physics example", error, capacity);
    } catch (const std::exception& e) {
        if (capacity)
            std::snprintf(error, capacity, "%s", e.what());
        return 0;
    }
}
