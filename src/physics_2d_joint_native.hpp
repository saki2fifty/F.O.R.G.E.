#pragma once
#include "physics_2d_joint.hpp"
#include <box2d/box2d.h>
#include <numbers>

namespace forge::physics_detail {
inline constexpr float joint_radians_2d = std::numbers::pi_v<float> / 180;
inline void joint_base_2d(b2JointDef& base, const Joint2DDesired& value, b2BodyId a, b2BodyId b) {
    base.bodyIdA = a;
    base.bodyIdB = b;
    base.localFrameA.p = {value.anchor_a[0], value.anchor_a[1]};
    base.localFrameB.p = {value.anchor_b[0], value.anchor_b[1]};
    if (value.config.kind != 0) {
        base.localFrameA.q = b2MakeRot(value.config.frame_a_angle * joint_radians_2d);
        base.localFrameB.q = b2MakeRot(value.config.frame_b_angle * joint_radians_2d);
    }
    base.collideConnected = value.config.collide_connected;
}
inline b2JointId create_joint_2d(b2WorldId world, const Joint2DDesired& value, b2BodyId a,
                                 b2BodyId b) {
    const auto& c = value.config;
    b2JointId result{};
    if (c.kind == 0) {
        auto definition = b2DefaultDistanceJointDef();
        joint_base_2d(definition.base, value, a, b);
        definition.length = c.distance;
        definition.enableSpring = c.spring_frequency > 0;
        definition.hertz = c.spring_frequency;
        definition.dampingRatio = c.spring_damping;
        definition.enableLimit = definition.enableMotor = false;
        result = b2CreateDistanceJoint(world, &definition);
    } else if (c.kind == 1) {
        auto definition = b2DefaultRevoluteJointDef();
        joint_base_2d(definition.base, value, a, b);
        definition.enableLimit = c.limits_enabled;
        definition.lowerAngle = c.min_angle * joint_radians_2d;
        definition.upperAngle = c.max_angle * joint_radians_2d;
        definition.enableMotor = c.motor_enabled;
        definition.motorSpeed = c.hinge_motor_speed * joint_radians_2d;
        definition.maxMotorTorque = c.max_motor_torque;
        definition.enableSpring = false;
        result = b2CreateRevoluteJoint(world, &definition);
    } else {
        auto definition = b2DefaultPrismaticJointDef();
        joint_base_2d(definition.base, value, a, b);
        definition.enableLimit = c.limits_enabled;
        definition.lowerTranslation = c.min_travel;
        definition.upperTranslation = c.max_travel;
        definition.enableMotor = c.motor_enabled;
        definition.motorSpeed = c.slider_motor_speed;
        definition.maxMotorForce = c.max_motor_force;
        definition.enableSpring = false;
        result = b2CreatePrismaticJoint(world, &definition);
    }
    if (!b2Joint_IsValid(result))
        throw std::runtime_error("Native 2D joint creation failed");
    return result;
}
inline void update_joint_2d(b2JointId joint, const PhysicsJoint2D& a, const PhysicsJoint2D& b) {
    if (a.collide_connected != b.collide_connected)
        b2Joint_SetCollideConnected(joint, b.collide_connected);
    if (b.kind == 0) {
        if (a.distance != b.distance)
            b2DistanceJoint_SetRestLength(joint, b.distance);
        if ((a.spring_frequency > 0) != (b.spring_frequency > 0))
            b2DistanceJoint_EnableSpring(joint, b.spring_frequency > 0);
        if (a.spring_frequency != b.spring_frequency)
            b2DistanceJoint_SetSpringHertz(joint, b.spring_frequency);
        if (a.spring_damping != b.spring_damping)
            b2DistanceJoint_SetSpringDampingRatio(joint, b.spring_damping);
    } else if (b.kind == 1) {
        if (a.limits_enabled != b.limits_enabled)
            b2RevoluteJoint_EnableLimit(joint, b.limits_enabled);
        if (a.min_angle != b.min_angle || a.max_angle != b.max_angle)
            b2RevoluteJoint_SetLimits(joint, b.min_angle * joint_radians_2d,
                                      b.max_angle * joint_radians_2d);
        if (a.motor_enabled != b.motor_enabled)
            b2RevoluteJoint_EnableMotor(joint, b.motor_enabled);
        if (a.hinge_motor_speed != b.hinge_motor_speed)
            b2RevoluteJoint_SetTargetMotorSpeed(joint, b.hinge_motor_speed * joint_radians_2d);
        if (a.max_motor_torque != b.max_motor_torque)
            b2RevoluteJoint_SetMaxMotorTorque(joint, b.max_motor_torque);
    } else {
        if (a.limits_enabled != b.limits_enabled)
            b2PrismaticJoint_EnableLimit(joint, b.limits_enabled);
        if (a.min_travel != b.min_travel || a.max_travel != b.max_travel)
            b2PrismaticJoint_SetLimits(joint, b.min_travel, b.max_travel);
        if (a.motor_enabled != b.motor_enabled)
            b2PrismaticJoint_EnableMotor(joint, b.motor_enabled);
        if (a.slider_motor_speed != b.slider_motor_speed)
            b2PrismaticJoint_SetTargetMotorSpeed(joint, b.slider_motor_speed);
        if (a.max_motor_force != b.max_motor_force)
            b2PrismaticJoint_SetMaxMotorForce(joint, b.max_motor_force);
    }
    // Exact native setters do not wake sleeping islands. Inactive edits and an
    // unchanged synchronization must still allow ordinary sleep to persist.
    if (joint_controls_changed_2d(a, b))
        b2Joint_WakeBodies(joint);
}
inline void retire_joint_2d(b2JointId joint) {
    // Native destruction does not mark an unmoved proxy for pair discovery.
    // Restore permission before removal; another active opt-out still wins.
    if (!b2Joint_GetCollideConnected(joint))
        b2Joint_SetCollideConnected(joint, true);
    b2Joint_WakeBodies(joint);
    b2DestroyJoint(joint);
}
inline bool joint_matches_2d(b2JointId joint, const Joint2DDesired& value, b2BodyId a, b2BodyId b) {
    const auto& c = value.config;
    const auto kind = c.kind == 0   ? b2_distanceJoint
                      : c.kind == 1 ? b2_revoluteJoint
                                    : b2_prismaticJoint;
    if (!b2Joint_IsValid(joint) || b2Joint_GetType(joint) != kind ||
        !B2_ID_EQUALS(b2Joint_GetBodyA(joint), a) || !B2_ID_EQUALS(b2Joint_GetBodyB(joint), b) ||
        b2Joint_GetCollideConnected(joint) != c.collide_connected || b2Joint_GetUserData(joint))
        return false;
    auto defaults = c.kind == 0   ? b2DefaultDistanceJointDef().base
                    : c.kind == 1 ? b2DefaultRevoluteJointDef().base
                                  : b2DefaultPrismaticJointDef().base;
    const auto original = defaults;
    joint_base_2d(defaults, value, a, b);
    const auto frame_equal = [](b2Transform p, b2Transform q) {
        return p.p.x == q.p.x && p.p.y == q.p.y && p.q.c == q.q.c && p.q.s == q.q.s;
    };
    float hertz = 0, damping = 0;
    b2Joint_GetConstraintTuning(joint, &hertz, &damping);
    if (!frame_equal(b2Joint_GetLocalFrameA(joint), defaults.localFrameA) ||
        !frame_equal(b2Joint_GetLocalFrameB(joint), defaults.localFrameB) ||
        hertz != original.constraintHertz || damping != original.constraintDampingRatio ||
        b2Joint_GetForceThreshold(joint) != original.forceThreshold ||
        b2Joint_GetTorqueThreshold(joint) != original.torqueThreshold)
        return false;
    if (c.kind == 0) {
        const auto definition = b2DefaultDistanceJointDef();
        float lower = 0, upper = 0;
        b2DistanceJoint_GetSpringForceRange(joint, &lower, &upper);
        return b2DistanceJoint_GetRestLength(joint) == c.distance &&
               b2DistanceJoint_IsSpringEnabled(joint) == (c.spring_frequency > 0) &&
               b2DistanceJoint_GetSpringHertz(joint) == c.spring_frequency &&
               b2DistanceJoint_GetSpringDampingRatio(joint) == c.spring_damping &&
               lower == definition.lowerSpringForce && upper == definition.upperSpringForce &&
               !b2DistanceJoint_IsLimitEnabled(joint) && !b2DistanceJoint_IsMotorEnabled(joint);
    }
    if (c.kind == 1)
        return b2RevoluteJoint_IsLimitEnabled(joint) == c.limits_enabled &&
               b2RevoluteJoint_GetLowerLimit(joint) == c.min_angle * joint_radians_2d &&
               b2RevoluteJoint_GetUpperLimit(joint) == c.max_angle * joint_radians_2d &&
               b2RevoluteJoint_IsMotorEnabled(joint) == c.motor_enabled &&
               b2RevoluteJoint_GetTargetMotorSpeed(joint) ==
                   c.hinge_motor_speed * joint_radians_2d &&
               b2RevoluteJoint_GetMaxMotorTorque(joint) == c.max_motor_torque &&
               !b2RevoluteJoint_IsSpringEnabled(joint);
    return b2PrismaticJoint_IsLimitEnabled(joint) == c.limits_enabled &&
           b2PrismaticJoint_GetLowerLimit(joint) == c.min_travel &&
           b2PrismaticJoint_GetUpperLimit(joint) == c.max_travel &&
           b2PrismaticJoint_IsMotorEnabled(joint) == c.motor_enabled &&
           b2PrismaticJoint_GetTargetMotorSpeed(joint) == c.slider_motor_speed &&
           b2PrismaticJoint_GetMaxMotorForce(joint) == c.max_motor_force &&
           !b2PrismaticJoint_IsSpringEnabled(joint);
}
} // namespace forge::physics_detail
