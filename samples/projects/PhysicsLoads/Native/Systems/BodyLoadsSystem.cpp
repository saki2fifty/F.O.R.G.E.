#include "../Components/BodyLoads.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <forge/native_sdk_identity.h>
#include <forge/physics_components.hpp>
#include <forge/transform_components.hpp>
#include <stdexcept>

void forge_register_system_BodyLoadsSystem(const ForgeSdkWorldV1* host) {
    flecs::world world(host->world);
    world.system<BodyLoads, const forge::LocalTranslation>("gameplay.BodyLoadsSystem")
        .kind(host->fixed_phase)
        .each([host](flecs::iter& it, size_t row, BodyLoads& data,
                     const forge::LocalTranslation& position) {
            if (data.kind > FORGE_SDK_PHYSICS_ANGULAR_IMPULSE || !std::isfinite(data.duration) ||
                data.duration < 0 || !std::isfinite(data.elapsed) || data.elapsed < 0)
                throw std::runtime_error("Invalid Body Loads example settings");
            const bool continuous =
                data.kind == FORGE_SDK_PHYSICS_FORCE || data.kind == FORGE_SDK_PHYSICS_TORQUE;
            const double dt = it.delta_time();
            if (continuous ? data.elapsed < data.duration : !data.triggered) {
                const double duty =
                    continuous ? std::min(dt, data.duration - data.elapsed) / dt : 1;
                const double vector[] = {data.x * duty, data.y * duty, data.z * duty};
                const double point[] = {position.x + data.offset_x, position.y + data.offset_y,
                                        position.z + data.offset_z};
                ForgeSdkEntityRefV1 ref{};
                ref.size = sizeof(ref);
                if (host->entity_reference(host->context, it.entity(row).id(), &ref) != 1 ||
                    host->physics_apply(host->context, ref.scene_uuid, ref.entity_uuid, data.kind,
                                        vector, data.at_point ? point : nullptr) != 1)
                    throw std::runtime_error("Body Loads example rejected its physics command");
                data.triggered = true;
            }
            data.elapsed += dt;
        })
        .add(host->fixed_tag);
    // Sample observations run after integration. They are copied solver values,
    // never authored state or values predicted from the command above.
    world.component<forge::PhysicsBody>("forge.physics_body");
    world.system<const BodyLoads, const forge::PhysicsBody>("gameplay.BodyLoadsObservation")
        .kind(host->post_physics_phase)
        .each([host](flecs::iter& it, size_t row, const BodyLoads& data,
                     const forge::PhysicsBody& body) {
            if (data.elapsed > it.delta_time() * 1.5)
                return; // Six initial observations, rather than a per-frame log.
            if (data.kind > FORGE_SDK_PHYSICS_ANGULAR_IMPULSE)
                throw std::runtime_error("Body Loads observation has an invalid load kind");
            const auto entity = it.entity(row);
            ForgeSdkEntityRefV1 ref{};
            ref.size = sizeof(ref);
            ForgeSdkPhysicsBodyStateV1 state{};
            state.size = sizeof(state);
            if (host->entity_reference(host->context, entity.id(), &ref) != 1 ||
                host->physics_body_state(host->context, ref.scene_uuid, ref.entity_uuid, &state) !=
                    1)
                throw std::runtime_error("Body Loads example could not observe its realized body");
            const auto magnitude = [](const double* value) {
                return std::hypot(value[0], value[1], value[2]);
            };
            char message[512];
            const char* kinds[] = {"Force", "Impulse", "Torque", "Angular impulse"};
            const auto label = data.at_point && data.kind == FORGE_SDK_PHYSICS_IMPULSE
                                   ? "Off-center impulse"
                                   : kinds[data.kind];
            std::snprintf(
                message, sizeof(message),
                "Body motion: %s (mass %.3g kg) | linear %.9f m/s | angular %.9f rad/s | %s", label,
                double(body.mass), magnitude(state.linear_velocity),
                magnitude(state.angular_velocity), state.active ? "active" : "inactive");
            if (host->diagnostic(host->context, 1, message) != 1)
                throw std::runtime_error("Body Loads example motion diagnostic failed");
        })
        .add(host->fixed_tag);
}
