#include "../Components/CharacterMovement2D.hpp"
#include <algorithm>
#include <cmath>
#include <forge/character_components.hpp>
#include <forge/sdk_client.hpp>
#include <stdexcept>

namespace {
ForgeSdkEntityRefV1 reference(const ForgeSdkWorldV1* host, flecs::entity entity) {
    ForgeSdkEntityRefV1 ref{};
    ref.size = sizeof(ref);
    if (host->entity_reference(host->context, entity.id(), &ref) != 1)
        throw std::runtime_error("Character Movement 2D needs an authored entity reference");
    return ref;
}
ForgeSdkCharacter2DV1 state(const forge::sdk::Client& client, const ForgeSdkEntityRefV1& ref) {
    ForgeSdkCharacter2DV1 value{};
    if (!client.character_2d(ref.scene_uuid, ref.entity_uuid, value))
        throw std::runtime_error("Character Movement 2D cannot read its controller");
    return value;
}
} // namespace
void forge_register_system_CharacterMovement2DSystem(const ForgeSdkWorldV1* host) {
    flecs::world world(host->world);
    world.component<forge::CharacterController2D>("forge.character_controller_2d");
    world
        .system<CharacterMovement2D, const forge::CharacterController2D>(
            "gameplay.CharacterMovement2DSystem")
        .kind(host->fixed_phase)
        .each([host](flecs::iter& it, size_t row, CharacterMovement2D& data,
                     const forge::CharacterController2D& controller) {
            if (!controller.enabled || controller.mode != 0 || !std::isfinite(data.speed) ||
                data.speed < 0 || data.speed > 20 || !std::isfinite(data.jump_speed) ||
                data.jump_speed < 0 || data.jump_speed > 20 || !std::isfinite(data.gravity) ||
                data.gravity < 0 || data.gravity > 100 || !std::isfinite(data.terminal_speed) ||
                data.terminal_speed < 1 || data.terminal_speed > 100)
                throw std::runtime_error(
                    "Character Movement 2D needs an enabled Grounded controller; "
                    "speed/jump_speed0..20m/s, gravity0..100m/s^2, terminal_speed1..100m/s");
            const forge::sdk::Client client(host);
            const auto ref = reference(host, it.entity(row));
            const auto observed = state(client, ref);
            ForgeSdkActionV1 move{}, jump{};
            if (!client.action("706ac33e-a66d-447b-bd53-cd3cff90dab4", move) ||
                !client.action("89892d64-e827-4b6c-a71f-2ae1551cc9bb", jump))
                throw std::runtime_error("Character Movement 2D cannot read project input");
            data.grounded = observed.ground == 0;
            // The engine clips this full velocity. Game C++ chooses gravity and
            // accepts a new jump only on the input edge while on walkable ground.
            const double vertical =
                data.grounded && jump.pressed
                    ? data.jump_speed
                    : std::max(-data.terminal_speed,
                               observed.velocity[1] - data.gravity * it.delta_time());
            const double velocity[]{std::clamp(move.x, -1., 1.) * data.speed, vertical};
            if (!client.move_character_2d(ref.scene_uuid, ref.entity_uuid, velocity))
                throw std::runtime_error("Character Movement 2D movement request was rejected");
        })
        .add(host->fixed_tag);
    world
        .system<CharacterMovement2D, const forge::CharacterController2D>(
            "gameplay.CharacterGround2DSystem")
        .kind(host->post_physics_phase)
        .each([host](flecs::entity entity, CharacterMovement2D& data,
                     const forge::CharacterController2D&) {
            data.grounded = state(forge::sdk::Client(host), reference(host, entity)).ground == 0;
        })
        .add(host->fixed_tag);
}
