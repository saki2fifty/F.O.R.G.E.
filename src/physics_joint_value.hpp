#pragma once
#include <forge/physics_components.hpp>
#include <nlohmann/json.hpp>
namespace forge::physics_detail {
inline void validate_joint(const PhysicsJoint& v) {
    for (const auto& ref : {v.body_a, v.body_b})
        if (bool(ref.scene) != bool(ref.entity))
            throw std::runtime_error("Joint body reference must be complete or empty");
    for (auto x :
         {v.anchor_a_x, v.anchor_a_y, v.anchor_a_z, v.anchor_b_x, v.anchor_b_y, v.anchor_b_z})
        if (!std::isfinite(x) || std::abs(x) > 10000)
            throw std::runtime_error("Joint anchors must be finite within +/-10000 meters");
    if (v.kind > 4 || v.axis_a > 2 || v.normal_a > 2 || v.axis_b > 2 || v.normal_b > 2 ||
        !std::isfinite(v.min_distance) || !std::isfinite(v.max_distance) ||
        v.min_distance < .001f || v.max_distance > 10000 || v.min_distance > v.max_distance ||
        !std::isfinite(v.min_angle) || !std::isfinite(v.max_angle) || v.min_angle < -180 ||
        v.min_angle > 0 || v.max_angle < 0 || v.max_angle > 180 || !std::isfinite(v.min_travel) ||
        !std::isfinite(v.max_travel) || v.min_travel < -10000 || v.min_travel > 0 ||
        v.max_travel < 0 || v.max_travel > 10000 || !std::isfinite(v.hinge_motor_speed) ||
        std::abs(v.hinge_motor_speed) > 36000 || !std::isfinite(v.slider_motor_speed) ||
        std::abs(v.slider_motor_speed) > 1000 || !std::isfinite(v.max_motor_torque) ||
        v.max_motor_torque < 0 || v.max_motor_torque > 1000000 ||
        !std::isfinite(v.max_motor_force) || v.max_motor_force < 0 || v.max_motor_force > 1000000 ||
        v.motor_mode > 1 || !std::isfinite(v.hinge_motor_angle) ||
        std::abs(v.hinge_motor_angle) > 180 || !std::isfinite(v.slider_motor_position) ||
        std::abs(v.slider_motor_position) > 10000 || !std::isfinite(v.motor_frequency) ||
        v.motor_frequency < .01f || v.motor_frequency > 100 || !std::isfinite(v.motor_damping) ||
        v.motor_damping < 0 || v.motor_damping > 10 ||
        ((v.kind == 1 || v.kind == 3 || v.kind == 4) &&
         (v.axis_a == v.normal_a || v.axis_b == v.normal_b)) ||
        (v.kind == 4 && v.min_travel == v.max_travel) ||
        (v.kind == 1 && v.min_angle == v.max_angle))
        throw std::runtime_error(
            "Invalid joint kind, distance/travel interval, frame axes, angle limits or motor");
    if (v.enabled && v.motor_enabled && v.motor_mode == 1 &&
        ((v.kind == 1 &&
          (v.hinge_motor_angle < v.min_angle || v.hinge_motor_angle > v.max_angle)) ||
         (v.kind == 4 &&
          (v.slider_motor_position < v.min_travel || v.slider_motor_position > v.max_travel))))
        throw std::runtime_error("Position motor target must be inside the joint limits");
}
inline bool joint_same_geometry(const PhysicsJoint& a, PhysicsJoint b) {
    b.motor_enabled = a.motor_enabled;
    b.hinge_motor_speed = a.hinge_motor_speed;
    b.slider_motor_speed = a.slider_motor_speed;
    b.max_motor_torque = a.max_motor_torque;
    b.max_motor_force = a.max_motor_force;
    b.motor_mode = a.motor_mode;
    b.hinge_motor_angle = a.hinge_motor_angle;
    b.slider_motor_position = a.slider_motor_position;
    b.motor_frequency = a.motor_frequency;
    b.motor_damping = a.motor_damping;
    b.collide_connected = a.collide_connected;
    return a == b;
}
inline bool joint_motor_changed(const PhysicsJoint& a, const PhysicsJoint& b) {
    if (b.kind != 1 && b.kind != 4)
        return false;
    if (a.motor_enabled != b.motor_enabled || a.motor_mode != b.motor_mode ||
        (b.kind == 1 ? a.max_motor_torque != b.max_motor_torque
                     : a.max_motor_force != b.max_motor_force))
        return true;
    if (b.motor_mode == 0)
        return b.kind == 1 ? a.hinge_motor_speed != b.hinge_motor_speed
                           : a.slider_motor_speed != b.slider_motor_speed;
    return a.motor_frequency != b.motor_frequency || a.motor_damping != b.motor_damping ||
           (b.kind == 1 ? a.hinge_motor_angle != b.hinge_motor_angle
                        : a.slider_motor_position != b.slider_motor_position);
}
inline nlohmann::json joint_value(const PhysicsJoint& v) {
    validate_joint(v);
    return {{"enabled", v.enabled},
            {"kind", v.kind},
            {"body_a", v.body_a.scene ? nlohmann::json(v.body_a) : nlohmann::json()},
            {"body_b", v.body_b.scene ? nlohmann::json(v.body_b) : nlohmann::json()},
            {"anchor_a_x", v.anchor_a_x},
            {"anchor_a_y", v.anchor_a_y},
            {"anchor_a_z", v.anchor_a_z},
            {"anchor_b_x", v.anchor_b_x},
            {"anchor_b_y", v.anchor_b_y},
            {"anchor_b_z", v.anchor_b_z},
            {"min_distance", v.min_distance},
            {"max_distance", v.max_distance},
            {"min_travel", v.min_travel},
            {"max_travel", v.max_travel},
            {"axis_a", v.axis_a},
            {"normal_a", v.normal_a},
            {"axis_b", v.axis_b},
            {"normal_b", v.normal_b},
            {"min_angle", v.min_angle},
            {"max_angle", v.max_angle},
            {"motor_enabled", v.motor_enabled},
            {"hinge_motor_speed", v.hinge_motor_speed},
            {"slider_motor_speed", v.slider_motor_speed},
            {"max_motor_torque", v.max_motor_torque},
            {"max_motor_force", v.max_motor_force},
            {"motor_mode", v.motor_mode},
            {"hinge_motor_angle", v.hinge_motor_angle},
            {"slider_motor_position", v.slider_motor_position},
            {"motor_frequency", v.motor_frequency},
            {"motor_damping", v.motor_damping},
            {"collide_connected", v.collide_connected}};
}
inline PhysicsJoint joint_from_value(const nlohmann::json& p) {
    PhysicsJoint v;
    v.enabled = p.at("enabled").get<bool>();
    v.kind = p.at("kind").get<std::uint32_t>();
    if (!p.at("body_a").is_null())
        v.body_a = p.at("body_a").get<EntityRef>();
    if (!p.at("body_b").is_null())
        v.body_b = p.at("body_b").get<EntityRef>();
    v.anchor_a_x = p.at("anchor_a_x");
    v.anchor_a_y = p.at("anchor_a_y");
    v.anchor_a_z = p.at("anchor_a_z");
    v.anchor_b_x = p.at("anchor_b_x");
    v.anchor_b_y = p.at("anchor_b_y");
    v.anchor_b_z = p.at("anchor_b_z");
    v.min_distance = p.at("min_distance");
    v.max_distance = p.at("max_distance");
    v.min_travel = p.value("min_travel", -1.f);
    v.max_travel = p.value("max_travel", 1.f);
    v.axis_a = p.at("axis_a").get<std::uint32_t>();
    v.normal_a = p.at("normal_a").get<std::uint32_t>();
    v.axis_b = p.at("axis_b").get<std::uint32_t>();
    v.normal_b = p.at("normal_b").get<std::uint32_t>();
    v.min_angle = p.at("min_angle");
    v.max_angle = p.at("max_angle");
    v.motor_enabled = p.value("motor_enabled", false);
    v.hinge_motor_speed = p.value("hinge_motor_speed", 90.f);
    v.slider_motor_speed = p.value("slider_motor_speed", 1.f);
    v.max_motor_torque = p.value("max_motor_torque", 10.f);
    v.max_motor_force = p.value("max_motor_force", 100.f);
    v.motor_mode = p.value("motor_mode", 0u);
    v.hinge_motor_angle = p.value("hinge_motor_angle", 0.f);
    v.slider_motor_position = p.value("slider_motor_position", 0.f);
    v.motor_frequency = p.value("motor_frequency", 2.f);
    v.motor_damping = p.value("motor_damping", 1.f);
    v.collide_connected = p.value("collide_connected", true);
    validate_joint(v);
    return v;
}
} // namespace forge::physics_detail
