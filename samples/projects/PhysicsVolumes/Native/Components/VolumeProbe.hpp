#pragma once
#include <cstdint>
#include <cstdio>
#include <exception>
#include <flecs.h>
#include <forge/native_sdk.h>
struct VolumeProbe {
    double radius = 1, speed = 1, center_x = 0, center_y = 0, center_z = 0;
    bool include_sensors = true, launched = false, observed = false;
    std::uint32_t last_count = 0;
};
inline int32_t forge_register_component_VolumeProbe(const ForgeSdkWorldV1* host, char* error,
                                                    uint32_t capacity) {
    try {
        flecs::world world(host->world);
        const auto type = world.component<VolumeProbe>("gameplay.VolumeProbe")
                              .member<double>("radius")
                              .member<double>("speed")
                              .member<double>("center_x")
                              .member<double>("center_y")
                              .member<double>("center_z")
                              .member<bool>("include_sensors")
                              .member<bool>("launched")
                              .member<bool>("observed")
                              .member<std::uint32_t>("last_count");
        ecs_doc_set_name(world, type, "Volume Probe");
        return host->authoring_type(
            host->context, type, "project.volume_probe", 1,
            R"({"radius":1,"speed":1,"center_x":0,"center_y":0,"center_z":0,"include_sensors":true,"launched":false,"observed":false,"last_count":0})",
            "Physics example", error, capacity);
    } catch (const std::exception& e) {
        if (capacity)
            std::snprintf(error, capacity, "%s", e.what());
        return 0;
    }
}
