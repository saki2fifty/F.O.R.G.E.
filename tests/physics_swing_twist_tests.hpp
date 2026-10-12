#pragma once
void prepare_swing_twist(Fixture& f) {
    f.setup();
    for (const auto* name : {"a", "b"}) {
        auto body = f.entity(name).get<PhysicsBody>();
        body.gravity_factor = 0;
        body.linear_damping = body.angular_damping = 0;
        body.allow_sleeping = false;
        f.entity(name).set<PhysicsBody>(body).set<BoxCollider>({.6, .6, .6});
        f.entity(name).set<LocalTranslation>({0, 0, 0});
    }
    auto joint = f.entity("j").get<PhysicsJoint>();
    joint.kind = 5;
    joint.axis_a = joint.axis_b = 0;
    joint.normal_a = joint.normal_b = 2;
    joint.min_angle = -20;
    joint.max_angle = 35;
    joint.swing_normal_angle = 40;
    joint.swing_plane_angle = 15;
    joint.collide_connected = false;
    f.entity("j").set<PhysicsJoint>(joint);
}
double single_axis_angle(Fixture& f, unsigned axis) {
    const auto q = f.entity("b").get<LocalRotation>();
    const double components[]{q.x, q.y, q.z};
    for (unsigned i = 0; i < 3; ++i)
        if (i != axis)
            require(std::abs(components[i]) < 1e-5, "Single-axis swing/twist coupled axes");
    return std::remainder(2 * std::atan2(components[axis], q.w) * 180 / std::numbers::pi, 360.);
}
void swing_twist_limits_and_recovery() {
    for (unsigned axis = 0; axis < 3; ++axis)
        for (int sign : {-1, 1}) {
            Fixture f, recovered;
            prepare_swing_twist(f);
            Double3 velocity{};
            velocity[axis] = sign * 2.;
            f.physics->set_angular_velocity(f.ref("b"), velocity);
            f.tick(4);
            // Independent free angular integration before reaching any limit.
            require(std::abs(single_axis_angle(f, axis) -
                             sign * 8 * double(1.f / 60) * 180 / std::numbers::pi) < .002,
                    "Swing Twist changed free motion inside its limits");
            const auto checkpoint = f.physics->checkpoint();
            recovered.scene.restore_snapshot(f.scene.snapshot());
            recovered.physics->restore(checkpoint);
            for (unsigned tick = 4; tick < 120; ++tick) {
                f.tick();
                recovered.tick();
                require(std::abs(single_axis_angle(f, axis) - single_axis_angle(recovered, axis)) <
                                1e-4 &&
                            distance(point(f, "b"), point(recovered, "b")) < 1e-5,
                        "Fresh swing/twist native recovery diverged");
            }
            const double bound = axis == 0 ? (sign < 0 ? 20. : 35.) : axis == 1 ? 15. : 40.;
            require(std::abs(single_axis_angle(f, axis) - sign * bound) < .15 &&
                        distance(point(f, "a"), point(f, "b")) < 1e-5,
                    "Native twist or unequal swing limit differs from its authored bound");
        }

    for (unsigned axis = 0; axis < 3; ++axis)
        for (float limit : {0.f, .49f, .5f, .51f, 179.49f, 179.5f, 179.51f, 180.f}) {
            Fixture f;
            prepare_swing_twist(f);
            auto joint = f.entity("j").get<PhysicsJoint>();
            joint.min_angle = -limit;
            joint.max_angle = limit;
            joint.swing_normal_angle = joint.swing_plane_angle = limit;
            f.entity("j").set<PhysicsJoint>(joint);
            Double3 velocity{};
            velocity[axis] = 2.;
            f.physics->set_angular_velocity(f.ref("b"), velocity);
            f.tick(120);
            const auto expected =
                limit < .5f ? 0.
                : limit > 179.5f
                    ? std::remainder(240 * double(1.f / 60) * 180 / std::numbers::pi, 360.)
                    : double(limit);
            require(std::abs(single_axis_angle(f, axis) - expected) < .15,
                    "Pinned swing/twist locked/free boundary differs");
        }

    Fixture ellipse, ellipse_recovered;
    prepare_swing_twist(ellipse);
    ellipse.physics->set_angular_velocity(ellipse.ref("b"), {0, 2, 2});
    double maximum = 0, radius = 0;
    for (unsigned tick = 0; tick < 240; ++tick) {
        if (tick == 120) {
            ellipse.physics->set_angular_velocity(ellipse.ref("b"), {});
            ellipse_recovered.physics->set_angular_velocity(ellipse_recovered.ref("b"), {});
        }
        ellipse.tick();
        const auto q = ellipse.entity("b").get<LocalRotation>();
        if (tick > 20) {
            ellipse_recovered.tick();
            const auto other = ellipse_recovered.entity("b").get<LocalRotation>();
            require(std::abs(q.x - other.x) < 1e-5 && std::abs(q.y - other.y) < 1e-5 &&
                        std::abs(q.z - other.z) < 1e-5 && std::abs(q.w - other.w) < 1e-5,
                    "Fresh coupled swing/twist recovery diverged");
        }
        if (tick == 20) {
            ellipse_recovered.scene.restore_snapshot(ellipse.scene.snapshot());
            ellipse_recovered.physics->restore(ellipse.physics->checkpoint());
        }
        // Independent q = swing * twist decomposition for the identity X frame.
        const auto length = std::hypot(q.x, q.w);
        require(length > .5, "Swing/twist cone reached an unexpected antipodal singularity");
        const auto tx = q.x / length, tw = q.w / length;
        const auto y = q.y * tw - q.z * tx, z = q.z * tw + q.y * tx;
        const auto normal = z / std::sin(40 * std::numbers::pi / 360);
        const auto plane = y / std::sin(15 * std::numbers::pi / 360);
        radius = normal * normal + plane * plane;
        require(std::isfinite(radius), "Combined swing generated a nonfinite cone measurement");
        if (tick >= 30 && tick < 120)
            maximum = std::max(maximum, radius);
    }
    // The native iterative position solver fixes only part of the error per
    // update (default Baumgarte0.2). Moving tangent to the cone can overshoot it.
    // After stopping angular motion, independently prove the actual settled
    // coupled orientation lies on the ellipse; do not assume a hard projection.
    require(maximum > .95 && radius <= 1.0001 && radius > .95,
            "Stopped combined swing failed its independent settled ellipse bound");

    Fixture reaction;
    prepare_swing_twist(reaction);
    auto body = reaction.entity("a").get<PhysicsBody>();
    body.motion = 2;
    body.mass = 1;
    reaction.entity("a").set<PhysicsBody>(body);
    reaction.physics->set_angular_velocity(reaction.ref("b"), {2, 0, 0});
    reaction.tick(120);
    const auto a = reaction.physics->body_state(reaction.ref("a"));
    const auto b = reaction.physics->body_state(reaction.ref("b"));
    require(a && b && std::abs(a->angular_velocity[0] - 1) < .002 &&
                std::abs(b->angular_velocity[0] - 1) < .002,
            "Equal-inertia swing/twist bodies lost angular-momentum reaction");

    Fixture invalid;
    prepare_swing_twist(invalid);
    invalid.tick();
    const auto original = invalid.entity("j").get<PhysicsJoint>();
    const auto checkpoint = invalid.physics->checkpoint();
    for (unsigned test = 0; test < 5; ++test) {
        auto joint = original;
        if (test == 0)
            joint.swing_normal_angle = -1;
        if (test == 1)
            joint.swing_plane_angle = 181;
        if (test == 2)
            joint.swing_normal_angle = std::numeric_limits<float>::quiet_NaN();
        if (test == 3)
            joint.normal_a = joint.axis_a;
        if (test == 4)
            joint.normal_b = joint.axis_b;
        invalid.entity("j").set<PhysicsJoint>(joint);
        rejects([&] { invalid.physics->synchronize(0); });
        invalid.entity("j").set<PhysicsJoint>(original);
        require(invalid.physics->checkpoint() == checkpoint,
                "Invalid swing/twist candidate mutated admitted solver state");
    }
}
