#pragma once
#include <cstdio>
#include <exception>
#include <flecs.h>
#include <forge/native_sdk.h>

// Project-owned selection for this example's rays, not global collision policy.
struct RayExclusion {
    bool enabled = true;
};
inline int32_t forge_register_component_RayExclusion(const ForgeSdkWorldV1* host, char* error,
                                                     uint32_t capacity) {
    try {
        flecs::world world(host->world);
        const auto type =
            world.component<RayExclusion>("gameplay.RayExclusion").member<bool>("enabled");
        ecs_doc_set_name(world, type, "Ray Exclusion");
        return host->authoring_type(host->context, type, "project.ray_exclusion", 1,
                                    R"({"enabled":true})", "Physics example", error, capacity);
    } catch (const std::exception& e) {
        if (capacity)
            std::snprintf(error, capacity, "%s", e.what());
        return 0;
    }
}
