#pragma once
#include <cstdio>
#include <exception>
#include <flecs.h>
#include <forge/native_sdk.h>
#include <forge/transform_components.hpp>

// Degrees per second. The Timeline can animate this ordinary authored field.
struct AnimatedSpin {
    float speed = 90;
};
inline int32_t forge_register_component_AnimatedSpin(const ForgeSdkWorldV1* host, char* error,
                                                     uint32_t capacity) {
    try {
        flecs::world world(host->world);
        world.component<forge::LocalRotation>("forge.local_rotation");
        const auto type =
            world.component<AnimatedSpin>("gameplay.AnimatedSpin").member<float>("speed");
        ecs_doc_set_name(world, type, "Animated Spin");
        return host->authoring_type(host->context, type, "project.animated_spin", 1,
                                    R"({"speed":90})", "Animation example", error, capacity);
    } catch (const std::exception& why) {
        if (capacity)
            std::snprintf(error, capacity, "%s", why.what());
        return 0;
    }
}
