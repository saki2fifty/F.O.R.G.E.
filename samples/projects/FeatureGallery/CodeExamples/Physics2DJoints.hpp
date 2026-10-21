#pragma once
#include <flecs.h>
#include <forge/physics_components.hpp>
#include <stdexcept>

// Use from an owner-thread fixed Gameplay system, before Physics. Register the
// matching engine component alias in the module schema callback. These are
// runtime values; the saved authored Scene is not changed. Physics validates
// the complete candidate at its existing synchronization boundary.
inline void forge_gallery_drive_joint_2d(flecs::entity entity, float speed, float budget) {
    if (!entity || !entity.has<forge::PhysicsJoint2D>())
        throw std::runtime_error("Choose an entity with Physics Joint 2D");
    auto value = entity.get<forge::PhysicsJoint2D>();
    if (value.kind == 1) {
        value.hinge_motor_speed = speed; // Signed degrees/s; budget is N*m.
        value.max_motor_torque = budget;
    } else if (value.kind == 2) {
        value.slider_motor_speed = speed; // Signed metres/s; budget is N.
        value.max_motor_force = budget;
    } else
        throw std::runtime_error("Velocity motors require a Hinge or Slider 2D");
    value.motor_enabled = true;
    entity.set(value);
}
inline void forge_gallery_spring_joint_2d(flecs::entity entity, float length, float frequency,
                                          float damping) {
    if (!entity || !entity.has<forge::PhysicsJoint2D>())
        throw std::runtime_error("Choose an entity with Physics Joint 2D");
    auto value = entity.get<forge::PhysicsJoint2D>();
    if (value.kind != 0)
        throw std::runtime_error("This spring helper requires a Distance 2D joint");
    value.distance = length; // World metres; frequency0 is rigid, otherwise Hz.
    value.spring_frequency = frequency;
    value.spring_damping = damping;
    entity.set(value);
}
