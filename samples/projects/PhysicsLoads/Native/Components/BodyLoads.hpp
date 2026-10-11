#pragma once
#include <cstdint>
#include <cstdio>
#include <exception>
#include <flecs.h>
#include <forge/native_sdk.h>

// Example data: world load, two-second thrust or one impulse, optional point.
// Progress is reflected too, so isolated runtime recovery preserves one-shot intent.
struct BodyLoads {
    std::uint32_t kind = FORGE_SDK_PHYSICS_FORCE;
    double x = 0, y = 6, z = 0, duration = 2;
    bool at_point = false;
    double offset_x = 1, offset_y = 0, offset_z = 0;
    double elapsed = 0;
    bool triggered = false;
};
inline int32_t forge_register_component_BodyLoads(const ForgeSdkWorldV1* host, char* error,
                                                  uint32_t capacity) {
    try {
        flecs::world world(host->world);
        const auto type = world.component<BodyLoads>("gameplay.BodyLoads")
                              .member<std::uint32_t>("kind")
                              .member<double>("x")
                              .member<double>("y")
                              .member<double>("z")
                              .member<double>("duration")
                              .member<bool>("at_point")
                              .member<double>("offset_x")
                              .member<double>("offset_y")
                              .member<double>("offset_z")
                              .member<double>("elapsed")
                              .member<bool>("triggered");
        ecs_doc_set_name(world, type, "Body Loads");
        return host->authoring_type(
            host->context, type, "project.body_loads", 1,
            R"({"kind":0,"x":0,"y":6,"z":0,"duration":2,"at_point":false,"offset_x":1,"offset_y":0,"offset_z":0,"elapsed":0,"triggered":false})",
            "Physics example", error, capacity);
    } catch (const std::exception& e) {
        if (capacity)
            std::snprintf(error, capacity, "%s", e.what());
        return 0;
    } catch (...) {
        if (capacity)
            std::snprintf(error, capacity, "%s", "Body Loads registration failed");
        return 0;
    }
}
