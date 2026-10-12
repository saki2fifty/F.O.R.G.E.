#pragma once
#include "../physics_joint_value.hpp"
#include <array>
#include <forge/scene.hpp>
#include <forge/transform.hpp>
#include <optional>
namespace forge::ui {
struct PhysicsJointGuideFrame {
    bool available = false, enabled = false;
    unsigned motion = 0;
    Double3 anchor{};
    std::array<Double3, 3> axes{};
    std::string name, error;
};
struct PhysicsJointGuide {
    bool enabled = true, valid = false;
    unsigned kind = 5; // Invalid/unavailable settings until the shared codec admits them.
    float min_travel = 0, max_travel = 0;
    bool motor_enabled = false;
    float motor_speed = 0, motor_budget = 0;
    unsigned motor_mode = 0;
    float motor_target = 0, motor_frequency = 0, motor_damping = 0;
    std::array<unsigned, 2> axis{}, normal{};
    std::array<PhysicsJointGuideFrame, 2> bodies;
    std::string error;
};
// Derived selected-object data only; no live body, asset job, history or document owner.
inline std::optional<PhysicsJointGuide> prepare_physics_joint_guide(const Json& scene,
                                                                    const std::string& selected) {
    const Json* joint_row = nullptr;
    for (const auto& row : scene.at("entities"))
        if (row.at("id") == selected) {
            joint_row = &row;
            break;
        }
    if (!joint_row || !joint_row->at("components").contains("forge.physics_joint"))
        return {};
    PhysicsJointGuide guide;
    try {
        const auto joint =
            physics_detail::joint_from_value(joint_row->at("components").at("forge.physics_joint"));
        guide.enabled = joint.enabled;
        guide.kind = joint.kind;
        guide.min_travel = joint.min_travel;
        guide.motor_enabled = joint.motor_enabled;
        guide.motor_speed = joint.kind == 1 ? joint.hinge_motor_speed : joint.slider_motor_speed;
        guide.motor_budget = joint.kind == 1 ? joint.max_motor_torque : joint.max_motor_force;
        guide.motor_mode = joint.motor_mode;
        guide.motor_target =
            joint.kind == 1 ? joint.hinge_motor_angle : joint.slider_motor_position;
        guide.motor_frequency = joint.motor_frequency;
        guide.motor_damping = joint.motor_damping;
        guide.max_travel = joint.max_travel;
        guide.axis = {joint.axis_a, joint.axis_b};
        guide.normal = {joint.normal_a, joint.normal_b};
        const std::array refs{joint.body_a, joint.body_b};
        const std::array anchors{Double3{joint.anchor_a_x, joint.anchor_a_y, joint.anchor_a_z},
                                 Double3{joint.anchor_b_x, joint.anchor_b_y, joint.anchor_b_z}};
        for (unsigned i = 0; i < 2; ++i) {
            auto& frame = guide.bodies[i];
            try {
                if (!refs[i].scene || !refs[i].entity)
                    throw std::runtime_error("Choose a body");
                if (Json(refs[i].scene) != scene.at("asset_id"))
                    throw std::runtime_error("Body is in another scene");
                const Json* body = nullptr;
                for (const auto& row : scene.at("entities"))
                    if (row.at("id") == refs[i].entity.str()) {
                        if (body)
                            throw std::runtime_error("Body reference is ambiguous");
                        body = &row;
                    }
                if (!body)
                    throw std::runtime_error("Body is unavailable");
                frame.name = body->value("name", std::string("Body"));
                const auto& values = body->at("components");
                if (!values.contains("forge.physics_body"))
                    throw std::runtime_error("Target has no Physics Body");
                const auto& config = values.at("forge.physics_body");
                frame.enabled = config.value("enabled", true) && !config.value("sensor", false);
                frame.motion = config.at("motion");
                if (!body->value("spatial_resolved", false))
                    throw std::runtime_error("Body transform is unresolved");
                AffineTransform world;
                world.m = body->at("world_affine").get<std::array<double, 12>>();
                LocalTransform hint;
                if (values.contains("forge.local_scale")) {
                    const auto& v = values.at("forge.local_scale");
                    hint.scale = {v.at("x"), v.at("y"), v.at("z")};
                }
                if (values.contains("forge.local_rotation")) {
                    const auto& v = values.at("forge.local_rotation");
                    hint.rotation = {v.at("x"), v.at("y"), v.at("z"), v.at("w")};
                }
                auto pose = decompose(world, &hint);
                const Double3 scaled{float(anchors[i][0]) * pose.scale.x,
                                     float(anchors[i][1]) * pose.scale.y,
                                     float(anchors[i][2]) * pose.scale.z};
                for (auto value : scaled)
                    if (!std::isfinite(value) || std::abs(value) > 10000)
                        throw std::runtime_error("Scaled anchor exceeds +/-10000 meters");
                pose.scale = {1, 1, 1};
                const auto rigid = affine_transform(pose);
                frame.anchor = rigid.point(scaled);
                frame.axes = {rigid.vector({1, 0, 0}), rigid.vector({0, 1, 0}),
                              rigid.vector({0, 0, 1})};
                frame.available = true;
                if (!frame.enabled)
                    frame.error = "Body disabled or a sensor";
            } catch (const std::exception& e) {
                frame.error = e.what();
            }
        }
        guide.valid = refs[0] != refs[1] && guide.bodies[0].available &&
                      guide.bodies[1].available && guide.bodies[0].enabled &&
                      guide.bodies[1].enabled &&
                      (guide.bodies[0].motion == 2 || guide.bodies[1].motion == 2);
        if (refs[0] == refs[1])
            guide.error = "Choose two distinct bodies";
        else if (guide.bodies[0].available && guide.bodies[1].available &&
                 guide.bodies[0].motion != 2 && guide.bodies[1].motion != 2)
            guide.error = "At least one body must be Dynamic";
    } catch (const std::exception& e) {
        guide.error = e.what();
    }
    return guide;
}
} // namespace forge::ui
