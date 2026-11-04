#include "ParallelMotion.hpp"
#include <cmath>
#include <cstdio>
#include <exception>
#include <flecs.h>
#include <forge/transform_components.hpp>

int32_t forge_register_component_ParallelMotion(const ForgeSdkWorldV1* host, char* error,
                                                uint32_t capacity) {
    try {
        flecs::world world(host->world);
        auto type =
            world.component<ParallelMotion>("gameplay.ParallelMotion").member<double>("speed");
        type.set_doc_name("Parallel Motion");
        type.set_doc_brief("Moves an owned local translation along X; speed is local metres per "
                           "fixed simulation second. The system uses native worker stages.");
        return host->authoring_type(host->context, type, "project.parallelmotion", 1,
                                    R"({"speed":1})", "Gameplay", error, capacity);
    } catch (const std::exception& exception) {
        if (capacity)
            std::snprintf(error, capacity, "%s", exception.what());
    } catch (...) {
        if (capacity)
            std::snprintf(error, capacity, "Parallel Motion registration failed");
    }
    return 0;
}
void forge_register_system_ParallelMotion(const ForgeSdkWorldV1* host) {
    flecs::world world(host->world);
    world.component<forge::LocalTranslation>("forge.local_translation");
    world.system<const ParallelMotion, forge::LocalTranslation>("gameplay.ParallelMotionUpdate")
        .kind(host->fixed_phase)
        .term_at(2)
        .self()
        .multi_threaded()
        .each([](flecs::iter& it, std::size_t row, const ParallelMotion& motion,
                 forge::LocalTranslation& position) {
            if (!std::isfinite(motion.speed))
                return;
            const double next = position.x + motion.speed * it.delta_time();
            if (std::isfinite(next) && next != position.x) {
                position.x = next;
                // The iterator entity retains its native stage. Notification is
                // deferred to the owner merge, rather than using the root host.
                it.entity(row).modified<forge::LocalTranslation>();
            }
        })
        .add(host->fixed_tag);
}
