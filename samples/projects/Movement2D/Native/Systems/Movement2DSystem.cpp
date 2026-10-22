#include "../Components/Movement2D.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <forge/physics_components.hpp>
#include <forge/sdk_client.hpp>
#include <forge/transform_components.hpp>
#include <stdexcept>

void forge_register_system_Movement2DSystem(const ForgeSdkWorldV1* host) {
    flecs::world world(host->world);
    world.component<forge::PhysicsBody2D>("forge.physics_body_2d");
    world.component<forge::CapsuleCollider2D>("forge.capsule_collider_2d");
    world.component<forge::LocalScale>("forge.local_scale");
    world
        .system<Movement2D, const forge::PhysicsBody2D, const forge::CapsuleCollider2D,
                const forge::LocalScale>("gameplay.Movement2DSystem")
        .kind(host->fixed_phase)
        .each([host](flecs::entity entity, Movement2D& data, const forge::PhysicsBody2D& body,
                     const forge::CapsuleCollider2D& capsule, const forge::LocalScale& scale) {
            if (!std::isfinite(data.speed) || data.speed < 0 || data.speed > 20 ||
                !std::isfinite(data.jump_speed) || data.jump_speed < 0 || data.jump_speed > 20 ||
                !std::isfinite(data.ground_probe) || data.ground_probe < .02 ||
                data.ground_probe > 1 || !body.enabled || body.motion != 2 || body.sensor ||
                !body.lock_rotation || scale != forge::LocalScale{1, 1, 1})
                throw std::runtime_error(
                    "2D Movement needs a unit-scale Dynamic solid capsule with locked rotation; "
                    "speed/jump_speed0..20m/s, ground_probe0.02..1m");
            forge::sdk::Client client(host);
            ForgeSdkEntityRefV1 ref{};
            ref.size = sizeof(ref);
            ForgeSdkPhysicsBodyState2DV1 state{};
            ForgeSdkActionV1 move{}, jump{};
            if (host->entity_reference(host->context, entity.id(), &ref) != 1 ||
                client.body_state_2d(ref.scene_uuid, ref.entity_uuid, state) != 1 ||
                !client.action("706ac33e-a66d-447b-bd53-cd3cff90dab4", move) ||
                !client.action("89892d64-e827-4b6c-a71f-2ae1551cc9bb", jump))
                throw std::runtime_error("2D Movement cannot read its body or project input");

            ForgeSdkSweep2DV1 probe{};
            probe.shape = FORGE_SDK_PHYSICS_2D_CAPSULE;
            probe.layer_mask = data.floor_mask;
            probe.ignore_initial_overlap = 1; // A touching side wall is not the floor.
            probe.dimensions[0] = capsule.radius;
            probe.dimensions[1] = capsule.height;
            // Start slightly above the body to avoid treating its resting skin
            // contact as an initial overlap. This is a support probe, not recovery.
            probe.origin[0] = state.position[0];
            probe.origin[1] = state.position[1] + .03;
            probe.angle = state.angle;
            probe.displacement[1] = -(.03 + data.ground_probe);
            std::memcpy(probe.ignore_scene, ref.scene_uuid, sizeof(ref.scene_uuid));
            std::memcpy(probe.ignore_entity, ref.entity_uuid, sizeof(ref.entity_uuid));
            ForgeSdkPhysicsHit2DV1 support{};
            const auto result = client.shape_cast_2d(probe, support);
            if (result < 0)
                throw std::runtime_error("2D Movement support sweep failed");
            // A wall or ceiling is not support. Requiring non-rising velocity
            // prevents another jump while the capsule is leaving the floor.
            data.grounded =
                result == 1 && support.normal[1] >= .65 && state.linear_velocity[1] <= .5;
            const double velocity[] = {std::clamp(move.x, -1., 1.) * data.speed,
                                       data.grounded && jump.pressed ? data.jump_speed
                                                                     : state.linear_velocity[1]};
            if (!client.set_linear_velocity_2d(ref.scene_uuid, ref.entity_uuid, velocity))
                throw std::runtime_error("2D Movement velocity command rejected");
        })
        .add(host->fixed_tag);
}
