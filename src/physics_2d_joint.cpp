#include "physics_2d_joint.hpp"
#include "physics_2d.hpp"
#include "physics_joint_2d_value.hpp"
#include <forge/physics.hpp>
#include <vector>

namespace forge::physics_detail {
DesiredJoints2D desired_joints_2d(WorldContext& context, const Desired2D& bodies,
                                  std::uint64_t tick) {
    std::vector<std::pair<std::uint64_t, PhysicsJoint2D>> candidates;
    context.world().query<const PhysicsJoint2D>().each(
        [&](flecs::entity entity, const PhysicsJoint2D& joint) {
            if (joint.enabled) {
                if (candidates.size() == 4096)
                    throw std::runtime_error("Combined physics joint capacity4096 exceeded");
                candidates.emplace_back(entity.id(), joint);
            }
        });
    DesiredJoints2D result;
    for (const auto& [id, value] : candidates) {
        const auto ref = context.reference(id);
        auto related = value.body_a;
        try {
            validate_joint_2d(value);
            if (!ref || context.resolve(*ref).state != WorldContext::ResolveState::Available)
                throw std::runtime_error("2D joint requires unambiguous authored scene membership");
            const auto a = context.resolve(value.body_a), b = context.resolve(value.body_b);
            if (a.state != WorldContext::ResolveState::Available || !bodies.contains(a.entity))
                throw std::runtime_error(
                    "2D joint Body A must be an enabled 2D body in a loaded scene");
            related = value.body_b;
            if (b.state != WorldContext::ResolveState::Available || !bodies.contains(b.entity))
                throw std::runtime_error(
                    "2D joint Body B must be an enabled 2D body in a loaded scene");
            if (a.entity == b.entity)
                throw std::runtime_error("2D joint requires two different bodies");
            const auto& first = bodies.at(a.entity);
            const auto& second = bodies.at(b.entity);
            if (first.config.body.sensor || second.config.body.sensor ||
                (first.config.body.motion != 2 && second.config.body.motion != 2))
                throw std::runtime_error(
                    "2D joint requires nonsensor bodies and at least one Dynamic body");
            const auto scaled = [](float x, float y, LocalScale scale) {
                const std::array<double, 2> values{double(x) * scale.x, double(y) * scale.y};
                for (double coordinate : values)
                    if (!std::isfinite(coordinate) || std::abs(coordinate) > 10000)
                        throw std::runtime_error("Scaled 2D joint anchor exceeds +/-10000 metres");
                return std::array<float, 2>{float(values[0]), float(values[1])};
            };
            result.emplace(
                id, Joint2DDesired{*ref, value, a.entity, b.entity,
                                   scaled(value.anchor_a_x, value.anchor_a_y, first.pose.scale),
                                   scaled(value.anchor_b_x, value.anchor_b_y, second.pose.scale)});
        } catch (const std::exception& error) {
            Diagnostic diagnostic{Severity::Error, "physics.invalid_joint_2d", error.what(), {}};
            if (ref) {
                diagnostic.context.entity = ref->entity;
                diagnostic.context.asset = ref->scene;
            }
            if (related.scene && related.entity)
                diagnostic.context.related_entity = related;
            diagnostic.context.module = "forge.physics";
            diagnostic.context.property = "forge.physics_joint_2d";
            diagnostic.context.tick = tick;
            context.services().emit(diagnostic);
            throw PhysicsConfigurationError(std::move(diagnostic));
        }
    }
    return result;
}
bool joint_same_geometry_2d(const Joint2DDesired& a, const Joint2DDesired& b) {
    if (a.ref != b.ref || a.a != b.a || a.b != b.b || a.config.kind != b.config.kind ||
        a.anchor_a != b.anchor_a || a.anchor_b != b.anchor_b)
        return false;
    // Distance constraints do not use frame rotations. Inactive edits stay source
    // changes without needlessly rebuilding the unchanged native constraint.
    return b.config.kind == 0 || (a.config.frame_a_angle == b.config.frame_a_angle &&
                                  a.config.frame_b_angle == b.config.frame_b_angle);
}
bool joint_controls_changed_2d(const PhysicsJoint2D& a, const PhysicsJoint2D& b) {
    if (a.collide_connected != b.collide_connected)
        return true;
    if (b.kind == 0)
        return a.distance != b.distance || a.spring_frequency != b.spring_frequency ||
               (b.spring_frequency > 0 && a.spring_damping != b.spring_damping);
    if (a.limits_enabled != b.limits_enabled || a.motor_enabled != b.motor_enabled)
        return true;
    if (b.kind == 1)
        return (b.limits_enabled && (a.min_angle != b.min_angle || a.max_angle != b.max_angle)) ||
               (b.motor_enabled && (a.hinge_motor_speed != b.hinge_motor_speed ||
                                    a.max_motor_torque != b.max_motor_torque));
    return (b.limits_enabled && (a.min_travel != b.min_travel || a.max_travel != b.max_travel)) ||
           (b.motor_enabled && (a.slider_motor_speed != b.slider_motor_speed ||
                                a.max_motor_force != b.max_motor_force));
}
} // namespace forge::physics_detail
