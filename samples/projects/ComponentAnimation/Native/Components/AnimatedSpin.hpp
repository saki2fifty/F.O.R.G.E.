#pragma once
#include <cstdint>
#include <cstdio>
#include <exception>
#include <flecs.h>
#include <forge/native_sdk.h>
#include <forge/transform_components.hpp>

enum class SpinDirection : std::int32_t { Forward = 1, Reverse = -1 };
struct SpinSettings {
    // Degrees per second. Continuous keys change speed; held keys enable/reverse it.
    float speed = 90;
    bool enabled = true;
    SpinDirection direction = SpinDirection::Forward;
    float axis[3]{0, 1, 0}; // Local X/Y/Z; normalized by the system.
};
struct AnimatedSpin {
    SpinSettings settings;
};
inline int32_t forge_register_component_AnimatedSpin(const ForgeSdkWorldV1* host, char* error,
                                                     uint32_t capacity) {
    try {
        flecs::world world(host->world);
        world.component<forge::LocalRotation>("forge.local_rotation");
        world.component<SpinDirection>("gameplay.SpinDirection")
            .constant("Forward", SpinDirection::Forward)
            .constant("Reverse", SpinDirection::Reverse);
        world.component<SpinSettings>("gameplay.SpinSettings")
            .member<float>("speed")
            .member<bool>("enabled")
            .member<SpinDirection>("direction")
            .member<float>("axis", 3);
        const auto type =
            world.component<AnimatedSpin>("gameplay.AnimatedSpin").member<SpinSettings>("settings");
        ecs_doc_set_name(world, type, "Animated Spin");
        return host->authoring_type(
            host->context, type, "project.animated_spin", 4,
            R"({"settings":{"speed":90,"enabled":true,"direction":1,"axis":[0,1,0]}})",
            "Animation example", error, capacity);
    } catch (const std::exception& why) {
        if (capacity)
            std::snprintf(error, capacity, "%s", why.what());
        return 0;
    }
}
