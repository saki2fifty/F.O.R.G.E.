#pragma once
#include "../physics_2d_transform.hpp"
#include "../physics_joint_2d_value.hpp"
#include "../physics_joint_value.hpp"
#include "../spatial_document.hpp"
#include <array>
#include <forge/scene.hpp>
#include <forge/transform.hpp>
#include <numbers>
#include <optional>
#include <set>
#include <unordered_map>
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
    bool planar = false, limits_enabled = true;
    // Shared display geometry kinds; never serialized or used as native kind IDs.
    unsigned kind = 6; // Invalid/unavailable settings until the shared codec admits them.
    float min_travel = 0, max_travel = 0;
    float min_distance = 0, max_distance = 0;
    bool motor_enabled = false;
    bool collide_connected = true;
    float motor_speed = 0, motor_budget = 0;
    unsigned motor_mode = 0;
    float motor_target = 0, motor_frequency = 0, motor_damping = 0;
    float min_angle = 0, max_angle = 0, swing_normal_angle = 0, swing_plane_angle = 0;
    float distance_spring_frequency = 0, distance_spring_damping = 1;
    std::array<unsigned, 2> axis{}, normal{};
    std::array<PhysicsJointGuideFrame, 2> bodies;
    std::string error;
};
inline std::optional<PhysicsJointGuide>
prepare_physics_joint_2d_guide(const Json& scene, const std::string& selected) {
    std::unordered_map<std::string_view, const Json*> rows;
    std::set<std::string_view> ambiguous;
    rows.reserve(scene.at("entities").size());
    for (const auto& row : scene.at("entities")) {
        const auto& id = row.at("id").get_ref<const std::string&>();
        if (!rows.emplace(id, &row).second)
            ambiguous.insert(id);
    }
    const auto chosen = rows.find(selected);
    if (chosen == rows.end() ||
        !chosen->second->at("components").contains("forge.physics_joint_2d"))
        return {};
    PhysicsJointGuide guide;
    guide.planar = true;
    try {
        if (ambiguous.contains(selected))
            throw std::runtime_error("Joint reference is ambiguous");
        const auto value = physics_detail::joint_from_value_2d(
            chosen->second->at("components").at("forge.physics_joint_2d"));
        guide.enabled = value.enabled;
        guide.kind = value.kind == 2 ? 4 : value.kind;
        guide.limits_enabled = value.limits_enabled;
        guide.min_distance = guide.max_distance = value.distance;
        guide.distance_spring_frequency = value.spring_frequency;
        guide.distance_spring_damping = value.spring_damping;
        guide.min_angle = value.min_angle;
        guide.max_angle = value.max_angle;
        guide.min_travel = value.min_travel;
        guide.max_travel = value.max_travel;
        guide.motor_enabled = value.kind != 0 && value.motor_enabled;
        guide.motor_speed = value.kind == 1 ? value.hinge_motor_speed : value.slider_motor_speed;
        guide.motor_budget = value.kind == 1 ? value.max_motor_torque : value.max_motor_force;
        guide.collide_connected = value.collide_connected;
        guide.axis = value.kind == 1 ? std::array{2u, 2u} : std::array{0u, 0u};
        guide.normal = value.kind == 1 ? std::array{0u, 0u} : std::array{1u, 1u};
        const std::array refs{value.body_a, value.body_b};
        const std::array anchors{Double2{value.anchor_a_x, value.anchor_a_y},
                                 Double2{value.anchor_b_x, value.anchor_b_y}};
        const std::array angles{value.frame_a_angle, value.frame_b_angle};
        for (unsigned side = 0; side < 2; ++side) {
            auto& frame = guide.bodies[side];
            try {
                if (!refs[side].scene || !refs[side].entity)
                    throw std::runtime_error("Choose a 2D body");
                if (Json(refs[side].scene) != scene.at("asset_id"))
                    throw std::runtime_error("Body is in another scene");
                const auto found = rows.find(refs[side].entity.str());
                if (ambiguous.contains(refs[side].entity.str()))
                    throw std::runtime_error("Body reference is ambiguous");
                if (found == rows.end())
                    throw std::runtime_error("Body is unavailable");
                const auto* body = found->second;
                frame.name = body->value("name", std::string("Body"));
                const auto& components = body->at("components");
                if (!components.contains("forge.physics_body_2d"))
                    throw std::runtime_error("Target has no Physics Body 2D");
                const auto& config = components.at("forge.physics_body_2d");
                frame.enabled = config.value("enabled", true) && !config.value("sensor", false);
                frame.motion = config.at("motion");
                if (!body->value("spatial_resolved", false))
                    throw std::runtime_error("Body transform is unresolved");
                if (frame.motion == 2 && detail::read_binding(*body).mode != SpatialMode::World)
                    throw std::runtime_error("Dynamic 2D body requires World space");
                auto hint = detail::read_local(components);
                hint.scale = {1, 1, 1};
                std::set<const Json*> seen;
                for (const auto* node = body; node;) {
                    if (!seen.insert(node).second)
                        throw std::runtime_error("Spatial ancestry is cyclic");
                    const auto local = detail::read_local(node->at("components"));
                    physics_detail::validate_planar_rotation_2d(local.rotation);
                    hint.scale.x *= std::signbit(local.scale.x) ? -1.f : 1.f;
                    hint.scale.y *= std::signbit(local.scale.y) ? -1.f : 1.f;
                    hint.scale.z *=
                        local.scale.z == 0 ? 0.f : (std::signbit(local.scale.z) ? -1.f : 1.f);
                    const auto binding = detail::read_binding(*node);
                    if (binding.mode == SpatialMode::World)
                        break;
                    if (binding.mode == SpatialMode::Explicit &&
                        Json(binding.target.scene) != scene.at("asset_id"))
                        throw std::runtime_error("Spatial parent is in another scene");
                    const auto parent = binding.mode == SpatialMode::Explicit
                                            ? binding.target.entity.str()
                                            : node->value("parent", std::string{});
                    const auto next = rows.find(parent);
                    node =
                        next != rows.end() &&
                                next->second->at("components").contains("forge.local_translation")
                            ? next->second
                            : nullptr;
                }
                AffineTransform affine;
                affine.m = body->at("world_affine").get<std::array<double, 12>>();
                auto pose = physics_detail::checked_planar_pose_2d(affine, hint);
                const Double3 scaled{float(anchors[side][0] * pose.scale.x),
                                     float(anchors[side][1] * pose.scale.y), 0};
                for (const auto coordinate : scaled)
                    if (!std::isfinite(coordinate) || std::abs(coordinate) > 10000)
                        throw std::runtime_error("Scaled anchor exceeds +/-10000 metres");
                pose.scale = {1, 1, 1};
                const auto rigid = affine_transform(pose);
                frame.anchor = rigid.point(scaled);
                const auto angle = value.kind == 0 ? 0 : angles[side] * std::numbers::pi / 180;
                frame.axes = {rigid.vector({std::cos(angle), std::sin(angle), 0}),
                              rigid.vector({-std::sin(angle), std::cos(angle), 0}),
                              rigid.vector({0, 0, 1})};
                frame.available = true;
                if (!frame.enabled)
                    frame.error = "Body disabled or a sensor";
            } catch (const std::exception& error) {
                frame.error = error.what();
            }
        }
        guide.valid = refs[0] != refs[1] && guide.bodies[0].available &&
                      guide.bodies[1].available && guide.bodies[0].enabled &&
                      guide.bodies[1].enabled &&
                      (guide.bodies[0].motion == 2 || guide.bodies[1].motion == 2);
        if (value.enabled && refs[0] == refs[1])
            guide.error = "Choose two distinct 2D bodies";
        else if (value.enabled && guide.bodies[0].available && guide.bodies[1].available &&
                 guide.bodies[0].motion != 2 && guide.bodies[1].motion != 2)
            guide.error = "At least one body must be Dynamic";
    } catch (const std::exception& error) {
        guide.error = error.what();
    }
    return guide;
}
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
        guide.min_distance = joint.min_distance;
        guide.max_distance = joint.max_distance;
        guide.min_angle = joint.min_angle;
        guide.max_angle = joint.max_angle;
        guide.distance_spring_frequency = joint.distance_spring_frequency;
        guide.distance_spring_damping = joint.distance_spring_damping;
        guide.swing_normal_angle = joint.swing_normal_angle;
        guide.swing_plane_angle = joint.swing_plane_angle;
        guide.min_travel = joint.min_travel;
        guide.motor_enabled = joint.motor_enabled;
        guide.collide_connected = joint.collide_connected;
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
