#pragma once
// Private native implementation: include Jolt/Jolt.h first.
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/GroupFilter.h>
#include <forge/character_components.hpp>
#include <forge/transform.hpp>
namespace forge::physics_detail {
struct CharacterShapes {
    JPH::RefConst<JPH::Shape> standing, crouched;
    float radius = 0;
};
void validate_character(const CharacterController&);
CharacterShapes character_shapes(const CharacterController&, LocalScale);
// Owned by one PhysicsRuntime; system/allocator outlive this object. Operations
// run only at owner-thread fixed boundaries after native workers have joined.
class Character {
  public:
    Character(JPH::PhysicsSystem&, JPH::TempAllocator&, CharacterController, LocalTransform,
              const JPH::GroupFilter*, JPH::BodyID requested = {});
    void movement(Double3 world_planar_velocity);
    void jump(float speed);
    bool crouch(bool value);
    void step(float dt, Double3 gravity);
    bool place(LocalTranslation, LocalRotation, bool clear_velocity, JPH::BodyID ignore = {});
    bool last_jump_accepted() const { return last_jump_accepted_; }
    void refresh(JPH::BodyID ignore = {});
    JPH::CharacterVirtual& native() { return *character_; }
    const JPH::CharacterVirtual& native() const { return *character_; }
    bool crouched() const { return crouched_; }
    Double3 intent() const;
    Double3 takeoff_velocity() const;
    float pending_jump() const { return jump_; }
    // Host checkpoint records these copied controller-owned transient fields
    // alongside CharacterVirtual::SaveState; never authored scene/prefab values.
    void restore_motion(Double3 intent, Double3 takeoff, float jump, bool crouched,
                        bool jump_accepted = false);

  private:
    JPH::PhysicsSystem& system_;
    JPH::TempAllocator& allocator_;
    CharacterController config_;
    CharacterShapes shapes_;
    JPH::Ref<JPH::CharacterVirtual> character_;
    JPH::Vec3 intent_ = JPH::Vec3::sZero(), takeoff_ = JPH::Vec3::sZero();
    float jump_ = 0;
    bool crouched_ = false, inverted_ = false, last_jump_accepted_ = false;
};
} // namespace forge::physics_detail
