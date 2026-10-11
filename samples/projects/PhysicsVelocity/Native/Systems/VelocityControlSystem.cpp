#include "../Components/VelocityControl.hpp"
#include <cmath>
#include <stdexcept>
void forge_register_system_VelocityControlSystem(const ForgeSdkWorldV1* host) {
    flecs::world world(host->world);
    world.system<VelocityControl>("gameplay.VelocityControlSystem")
        .kind(host->fixed_phase)
        .each([host](flecs::iter& it, size_t row, VelocityControl& data) {
            if (data.stop_channel > 3 || !std::isfinite(data.stop_after) || data.stop_after < 0 ||
                !std::isfinite(data.elapsed) || data.elapsed < 0)
                throw std::runtime_error("Invalid velocity example settings");
            ForgeSdkEntityRefV1 ref{};
            ref.size = sizeof(ref);
            if (host->entity_reference(host->context, it.entity(row).id(), &ref) != 1)
                throw std::runtime_error("Velocity example identity unavailable");
            const auto set = [&](uint32_t channel, const double* vector) {
                if (host->physics_set_velocity(host->context, ref.scene_uuid, ref.entity_uuid,
                                               channel, vector) != 1)
                    throw std::runtime_error("Velocity example command rejected");
            };
            if (!data.launched) {
                const double linear[] = {data.linear_x, data.linear_y, data.linear_z};
                const double angular[] = {data.angular_x, data.angular_y, data.angular_z};
                set(FORGE_SDK_PHYSICS_LINEAR_VELOCITY, linear);
                set(FORGE_SDK_PHYSICS_ANGULAR_VELOCITY, angular);
                data.launched = true;
            }
            data.elapsed += it.delta_time();
            if (data.stop_channel && !data.stopped && data.elapsed >= data.stop_after) {
                const double zero[] = {0, 0, 0};
                if (data.stop_channel & 1)
                    set(FORGE_SDK_PHYSICS_LINEAR_VELOCITY, zero);
                if (data.stop_channel & 2)
                    set(FORGE_SDK_PHYSICS_ANGULAR_VELOCITY, zero);
                data.stopped = true;
            }
        })
        .add(host->fixed_tag);
    world.system<const VelocityControl>("gameplay.VelocityControlObservation")
        .kind(host->post_physics_phase)
        .each([host](flecs::iter& it, size_t row, const VelocityControl& data) {
            if (data.elapsed > it.delta_time() * 1.5 &&
                !(data.stopped && data.elapsed - data.stop_after < it.delta_time()))
                return;
            ForgeSdkEntityRefV1 ref{};
            ref.size = sizeof(ref);
            ForgeSdkPhysicsBodyStateV1 state{};
            state.size = sizeof(state);
            if (host->entity_reference(host->context, it.entity(row).id(), &ref) != 1 ||
                host->physics_body_state(host->context, ref.scene_uuid, ref.entity_uuid, &state) !=
                    1)
                throw std::runtime_error("Velocity example observation unavailable");
            char message[256];
            std::snprintf(message, sizeof(message),
                          "Velocity control: channel %u | linear %.9f m/s | angular %.9f rad/s",
                          data.stop_channel,
                          std::hypot(state.linear_velocity[0], state.linear_velocity[1],
                                     state.linear_velocity[2]),
                          std::hypot(state.angular_velocity[0], state.angular_velocity[1],
                                     state.angular_velocity[2]));
            if (host->diagnostic(host->context, 1, message) != 1)
                throw std::runtime_error("Velocity example diagnostic rejected");
        })
        .add(host->fixed_tag);
}
