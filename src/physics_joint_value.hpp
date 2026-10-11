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
    if (v.kind > 1 || v.axis_a > 2 || v.normal_a > 2 || v.axis_b > 2 || v.normal_b > 2 ||
        !std::isfinite(v.min_distance) || !std::isfinite(v.max_distance) ||
        v.min_distance < .001f || v.max_distance > 10000 || v.min_distance > v.max_distance ||
        !std::isfinite(v.min_angle) || !std::isfinite(v.max_angle) || v.min_angle < -180 ||
        v.min_angle > 0 || v.max_angle < 0 || v.max_angle > 180 ||
        (v.kind == 1 &&
         (v.axis_a == v.normal_a || v.axis_b == v.normal_b || v.min_angle == v.max_angle)))
        throw std::runtime_error(
            "Invalid joint kind, distance interval, hinge axes or angle limits");
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
            {"axis_a", v.axis_a},
            {"normal_a", v.normal_a},
            {"axis_b", v.axis_b},
            {"normal_b", v.normal_b},
            {"min_angle", v.min_angle},
            {"max_angle", v.max_angle}};
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
    v.axis_a = p.at("axis_a").get<std::uint32_t>();
    v.normal_a = p.at("normal_a").get<std::uint32_t>();
    v.axis_b = p.at("axis_b").get<std::uint32_t>();
    v.normal_b = p.at("normal_b").get<std::uint32_t>();
    v.min_angle = p.at("min_angle");
    v.max_angle = p.at("max_angle");
    validate_joint(v);
    return v;
}
} // namespace forge::physics_detail
