#pragma once
#include <forge/identity.hpp>
#include <forge/transform.hpp>
#include <optional>
namespace forge {
enum class CharacterGround : std::uint32_t { OnGround, OnSteepGround, NotSupported, InAir };
// Copied runtime observation, not authored prefab data or a durable save format.
struct CharacterState {
    LocalTranslation position;
    LocalRotation rotation;
    Double3 velocity{}, ground_normal{}, ground_velocity{}, ground_position{};
    std::optional<EntityRef> supporting_entity;
    CharacterGround ground = CharacterGround::InAir;
    bool crouched = false, shape_change_blocked = false, jump_accepted = false;
};
class CharacterService {
  public:
    virtual ~CharacterService() = default;
    virtual CharacterState character(EntityRef) const = 0;
    // Requests are copied and consumed at the next fixed physics boundary.
    // Movement is world-space velocity perpendicular to the character up axis.
    virtual void move_character(EntityRef, Double3 planar_velocity) = 0;
    virtual void jump_character(EntityRef, float speed) = 0;
    virtual void crouch_character(EntityRef, bool crouched) = 0;
    virtual void place_character(EntityRef, LocalTranslation, LocalRotation,
                                 bool clear_velocity) = 0;
};
} // namespace forge
