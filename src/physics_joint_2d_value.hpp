#pragma once
#include <forge/physics_components.hpp>
#include <nlohmann/json.hpp>

namespace forge::physics_detail {
// Exact selected native profile uses a five-millimetre stable minimum length.
inline constexpr float minimum_joint_distance_2d = .005f;
inline void validate_joint_2d(const PhysicsJoint2D& value) {
    for (const auto& ref : {value.body_a, value.body_b})
        if (bool(ref.scene) != bool(ref.entity))
            throw std::runtime_error("2D joint body reference must be complete or empty");
    const auto bounded = [](float x, float lower, float upper) {
        return std::isfinite(x) && x >= lower && x <= upper;
    };
    for (float anchor : {value.anchor_a_x, value.anchor_a_y, value.anchor_b_x, value.anchor_b_y})
        if (!bounded(anchor, -10000, 10000))
            throw std::runtime_error("2D joint anchors must be finite within +/-10000 metres");
    if (value.kind > 2 || !bounded(value.frame_a_angle, -180, 180) ||
        !bounded(value.frame_b_angle, -180, 180) ||
        !bounded(value.distance, minimum_joint_distance_2d, 10000) ||
        !bounded(value.spring_frequency, 0, 100) ||
        (value.spring_frequency > 0 && value.spring_frequency < .01f) ||
        !bounded(value.spring_damping, 0, 10) || !bounded(value.min_angle, -178, 178) ||
        !bounded(value.max_angle, -178, 178) || value.min_angle > value.max_angle ||
        !bounded(value.min_travel, -10000, 10000) || !bounded(value.max_travel, -10000, 10000) ||
        value.min_travel > value.max_travel || !bounded(value.hinge_motor_speed, -36000, 36000) ||
        !bounded(value.slider_motor_speed, -1000, 1000) ||
        !bounded(value.max_motor_torque, 0, 1000000) || !bounded(value.max_motor_force, 0, 1000000))
        throw std::runtime_error(
            "Invalid 2D joint kind, frames, distance, spring, limits or motor");
}
inline nlohmann::json joint_value_2d(const PhysicsJoint2D& v) {
    validate_joint_2d(v);
    return {{"enabled", v.enabled},
            {"kind", v.kind},
            {"body_a", v.body_a.scene ? nlohmann::json(v.body_a) : nlohmann::json()},
            {"body_b", v.body_b.scene ? nlohmann::json(v.body_b) : nlohmann::json()},
            {"anchor_a_x", v.anchor_a_x},
            {"anchor_a_y", v.anchor_a_y},
            {"anchor_b_x", v.anchor_b_x},
            {"anchor_b_y", v.anchor_b_y},
            {"frame_a_angle", v.frame_a_angle},
            {"frame_b_angle", v.frame_b_angle},
            {"distance", v.distance},
            {"spring_frequency", v.spring_frequency},
            {"spring_damping", v.spring_damping},
            {"limits_enabled", v.limits_enabled},
            {"min_angle", v.min_angle},
            {"max_angle", v.max_angle},
            {"min_travel", v.min_travel},
            {"max_travel", v.max_travel},
            {"motor_enabled", v.motor_enabled},
            {"hinge_motor_speed", v.hinge_motor_speed},
            {"slider_motor_speed", v.slider_motor_speed},
            {"max_motor_torque", v.max_motor_torque},
            {"max_motor_force", v.max_motor_force},
            {"collide_connected", v.collide_connected}};
}
inline PhysicsJoint2D joint_from_value_2d(const nlohmann::json& source) {
    PhysicsJoint2D v;
    v.enabled = source.at("enabled").get<bool>();
    v.kind = source.at("kind").get<std::uint32_t>();
    v.body_a = source.at("body_a").is_null() ? EntityRef{} : source.at("body_a").get<EntityRef>();
    v.body_b = source.at("body_b").is_null() ? EntityRef{} : source.at("body_b").get<EntityRef>();
    v.anchor_a_x = source.at("anchor_a_x");
    v.anchor_a_y = source.at("anchor_a_y");
    v.anchor_b_x = source.at("anchor_b_x");
    v.anchor_b_y = source.at("anchor_b_y");
    v.frame_a_angle = source.at("frame_a_angle");
    v.frame_b_angle = source.at("frame_b_angle");
    v.distance = source.at("distance");
    v.spring_frequency = source.at("spring_frequency");
    v.spring_damping = source.at("spring_damping");
    v.limits_enabled = source.at("limits_enabled").get<bool>();
    v.min_angle = source.at("min_angle");
    v.max_angle = source.at("max_angle");
    v.min_travel = source.at("min_travel");
    v.max_travel = source.at("max_travel");
    v.motor_enabled = source.at("motor_enabled").get<bool>();
    v.hinge_motor_speed = source.at("hinge_motor_speed");
    v.slider_motor_speed = source.at("slider_motor_speed");
    v.max_motor_torque = source.at("max_motor_torque");
    v.max_motor_force = source.at("max_motor_force");
    v.collide_connected = source.at("collide_connected").get<bool>();
    validate_joint_2d(v);
    return v;
}
} // namespace forge::physics_detail
