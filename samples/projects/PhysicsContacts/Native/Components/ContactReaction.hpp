#pragma once
#include <cstdint>
#include <cstdio>
#include <exception>
#include <flecs.h>
#include <forge/native_sdk.h>
struct ContactReaction {
    double speed = 2;
    bool reverse_on_begin = true, launched = false, reacted = false;
    std::uint32_t begin_count = 0, end_count = 0;
};
inline int32_t forge_register_component_ContactReaction(const ForgeSdkWorldV1* host, char* error,
                                                        uint32_t capacity) {
    try {
        flecs::world world(host->world);
        const auto type = world.component<ContactReaction>("gameplay.ContactReaction")
                              .member<double>("speed")
                              .member<bool>("reverse_on_begin")
                              .member<bool>("launched")
                              .member<bool>("reacted")
                              .member<std::uint32_t>("begin_count")
                              .member<std::uint32_t>("end_count");
        ecs_doc_set_name(world, type, "Contact Reaction");
        return host->authoring_type(
            host->context, type, "project.contact_reaction", 1,
            R"({"speed":2,"reverse_on_begin":true,"launched":false,"reacted":false,"begin_count":0,"end_count":0})",
            "Physics example", error, capacity);
    } catch (const std::exception& e) {
        if (capacity)
            std::snprintf(error, capacity, "%s", e.what());
        return 0;
    }
}
