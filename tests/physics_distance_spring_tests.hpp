#pragma once
#include <memory>
void prepare_distance_spring(Fixture& f, float mass = 1) {
    f.setup();
    for (const auto* name : {"a", "b"}) {
        auto body = f.entity(name).get<PhysicsBody>();
        body.gravity_factor = 0;
        body.linear_damping = body.angular_damping = 0;
        body.allow_sleeping = false;
        if (body.motion == 2)
            body.mass = mass;
        f.entity(name).set<PhysicsBody>(body).set<BoxCollider>({.2, .2, .2});
    }
    f.entity("a").set<LocalTranslation>({0, 0, 0});
    f.entity("b").set<LocalTranslation>({1.4, 0, 0});
    auto joint = f.entity("j").get<PhysicsJoint>();
    joint.min_distance = joint.max_distance = 1;
    joint.distance_spring_frequency = 1;
    joint.distance_spring_damping = 1;
    joint.collide_connected = false;
    f.entity("j").set<PhysicsJoint>(joint);
}
void distance_springs() {
    const double dt = double(1.f / 60), omega = 2 * std::numbers::pi;
    for (float mass : {1.f, 7.f})
        for (float damping : {0.f, .15f, 1.f, 2.f}) {
            Fixture f;
            prepare_distance_spring(f, mass);
            auto joint = f.entity("j").get<PhysicsJoint>();
            joint.distance_spring_damping = damping;
            f.entity("j").set<PhysicsJoint>(joint);
            double x = 1.4, v = 0, minimum = x;
            std::unique_ptr<Fixture> recovered;
            for (unsigned tick = 0; tick < 180; ++tick) {
                // Independently solve the mass-normalized spring by implicit Euler.
                v = (v - dt * omega * omega * (x - 1)) /
                    (1 + 2 * damping * omega * dt + omega * omega * dt * dt);
                x += dt * v;
                f.tick();
                const auto native = f.physics->body_state(f.ref("b"));
                require(native && std::abs(point(f, "b")[0] - x) < 1e-4 &&
                            std::abs(native->linear_velocity[0] - v) < 1e-4,
                        "Distance spring differs from independent implicit Euler integration");
                minimum = std::min(minimum, point(f, "b")[0]);
                if (tick == 20) {
                    recovered = std::make_unique<Fixture>();
                    recovered->scene.restore_snapshot(f.scene.snapshot());
                    recovered->physics->restore(f.physics->checkpoint());
                } else if (recovered) {
                    recovered->tick();
                    const auto restored = recovered->physics->body_state(recovered->ref("b"));
                    require(restored && distance(point(f, "b"), point(*recovered, "b")) < 1e-5 &&
                                std::abs(native->linear_velocity[0] -
                                         restored->linear_velocity[0]) < 1e-5,
                            "Fresh distance spring recovery diverged");
                }
            }
            require(
                damping < 1 ? minimum < .95 : minimum >= .9999,
                "Distance spring damping does not distinguish oscillation from monotonic return");
            if (damping >= .15f)
                require(std::abs(point(f, "b")[0] - 1) < .035,
                        "Damped distance spring failed to approach its rest length");
        }

    std::cout << "Spring independent integration and fresh recovery passed\n";
    Fixture reaction;
    prepare_distance_spring(reaction);
    auto body = reaction.entity("a").get<PhysicsBody>();
    body.motion = 2;
    body.mass = 3;
    reaction.entity("a").set<PhysicsBody>(body);
    double x = 1.4, v = 0;
    for (unsigned tick = 0; tick < 90; ++tick) {
        v = (v - dt * omega * omega * (x - 1)) / (1 + 2 * omega * dt + omega * omega * dt * dt);
        x += dt * v;
        reaction.tick();
        const auto a = reaction.physics->body_state(reaction.ref("a"));
        const auto b = reaction.physics->body_state(reaction.ref("b"));
        require(a && b && std::abs(3 * a->linear_velocity[0] + b->linear_velocity[0]) < 1e-5 &&
                    std::abs((3 * point(reaction, "a")[0] + point(reaction, "b")[0]) / 4 - .35) <
                        1e-5 &&
                    std::abs(point(reaction, "b")[0] - point(reaction, "a")[0] - x) < 1e-4,
                "Distance spring reaction lost mass-normalized motion or linear momentum");
    }

    std::cout << "Spring unequal-mass reaction passed\n";
    Fixture interval;
    prepare_distance_spring(interval);
    auto joint = interval.entity("j").get<PhysicsJoint>();
    joint.min_distance = .75;
    joint.max_distance = 1.25;
    interval.entity("j").set<PhysicsJoint>(joint);
    interval.entity("b").set<LocalTranslation>({1, 0, 0});
    interval.physics->set_linear_velocity(interval.ref("b"), {.3, 0, 0});
    interval.tick(5);
    require(std::abs(point(interval, "b")[0] - (1 + .3 * 5 * dt)) < 1e-6 &&
                std::abs(interval.physics->body_state(interval.ref("b"))->linear_velocity[0] - .3) <
                    1e-6,
            "Elastic distance interval applies force inside its free range");
    for (double start : {.6, 1.4}) {
        Fixture outside;
        prepare_distance_spring(outside);
        auto limits = outside.entity("j").get<PhysicsJoint>();
        limits.min_distance = joint.min_distance;
        limits.max_distance = joint.max_distance;
        outside.entity("j").set<PhysicsJoint>(limits);
        outside.entity("b").set<LocalTranslation>({start, 0, 0});
        double position = start, velocity = 0;
        const double target = start < .75 ? .75 : 1.25;
        for (unsigned tick = 0; tick < 4; ++tick) {
            velocity = (velocity - dt * omega * omega * (position - target)) /
                       (1 + 2 * omega * dt + omega * omega * dt * dt);
            position += dt * velocity;
            outside.tick();
            require(std::abs(point(outside, "b")[0] - position) < 1e-5 &&
                        std::abs(outside.physics->body_state(outside.ref("b"))->linear_velocity[0] -
                                 velocity) < 1e-5,
                    "Elastic distance interval pulls towards the wrong independent bound");
        }
    }

    std::cout << "Spring free interval and unilateral bounds passed\n";
    Fixture order;
    prepare_distance_spring(order);
    auto current = order.entity("j").get<PhysicsJoint>();
    auto attachment = current;
    attachment.kind = 2;
    order.entity("j2").set<PhysicsJoint>(attachment);
    attachment.kind = 3;
    order.entity("j3").set<PhysicsJoint>(attachment);
    order.physics->synchronize(0);
    const auto native_order = [](const Json& checkpoint) {
        Json result = Json::array();
        for (const auto& entry : checkpoint.at("joints"))
            result.push_back(entry.at("entity"));
        return result;
    };
    const auto original_order = native_order(order.physics->checkpoint());
    require(original_order.size() == 3 && original_order[0] == Json(order.ref("j")),
            "Distance spring order test did not start with a nonlast constraint");
    current.distance_spring_frequency = 2;
    current.distance_spring_damping = .2;
    order.entity("j").set<PhysicsJoint>(current);
    order.physics->synchronize(0);
    require(native_order(order.physics->checkpoint()) == original_order,
            "Spring-only settings retired the constraint and changed native order");
    const auto checkpoint = order.physics->checkpoint();
    for (unsigned invalid = 0; invalid < 6; ++invalid) {
        auto candidate = current;
        if (invalid == 0)
            candidate.distance_spring_frequency = -.01f;
        if (invalid == 1)
            candidate.distance_spring_frequency = .001f;
        if (invalid == 2)
            candidate.distance_spring_frequency = 101;
        if (invalid == 3)
            candidate.distance_spring_frequency = std::numeric_limits<float>::quiet_NaN();
        if (invalid == 4)
            candidate.distance_spring_damping = -1;
        if (invalid == 5)
            candidate.distance_spring_damping = 11;
        order.entity("j").set<PhysicsJoint>(candidate);
        rejects([&] { order.physics->synchronize(0); });
        order.entity("j").set<PhysicsJoint>(current);
        require(order.physics->checkpoint() == checkpoint,
                "Invalid distance spring candidate changed admitted native state");
    }
    auto legacy = physics_detail::joint_value(current);
    legacy.erase("distance_spring_frequency");
    legacy.erase("distance_spring_damping");
    const auto defaults = physics_detail::joint_from_value(legacy);
    require(defaults.distance_spring_frequency == 0 && defaults.distance_spring_damping == 1,
            "Legacy distance joint failed to backfill hard limits");

    std::cout << "Spring native order, invalid atomicity and legacy defaults passed\n";
    Fixture sleeping;
    prepare_distance_spring(sleeping);
    sleeping.entity("b").set<LocalTranslation>({1, 0, 0});
    body = sleeping.entity("b").get<PhysicsBody>();
    body.allow_sleeping = true;
    sleeping.entity("b").set<PhysicsBody>(body);
    current = sleeping.entity("j").get<PhysicsJoint>();
    current.distance_spring_frequency = 0;
    sleeping.entity("j").set<PhysicsJoint>(current);
    sleeping.tick(180);
    require(!sleeping.physics->body_state(sleeping.ref("b"))->active,
            "Distance spring wake test did not reach sleep");
    current.distance_spring_damping = .5;
    sleeping.entity("j").set<PhysicsJoint>(current);
    sleeping.physics->synchronize(0);
    require(!sleeping.physics->body_state(sleeping.ref("b"))->active,
            "Inactive spring damping woke a hard distance joint");
    current.distance_spring_frequency = 2;
    sleeping.entity("j").set<PhysicsJoint>(current);
    sleeping.physics->synchronize(0);
    require(sleeping.physics->body_state(sleeping.ref("b"))->active,
            "Spring enable failed to wake its moving endpoint");

    Fixture inactive;
    prepare_distance_spring(inactive);
    body = inactive.entity("b").get<PhysicsBody>();
    body.allow_sleeping = true;
    inactive.entity("b").set<PhysicsBody>(body);
    current = inactive.entity("j").get<PhysicsJoint>();
    current.kind = 4;
    current.axis_a = current.axis_b = 0;
    current.normal_a = current.normal_b = 1;
    current.min_travel = -20;
    current.max_travel = 20;
    current.motor_enabled = false;
    inactive.entity("j").set<PhysicsJoint>(current);
    inactive.tick(180);
    require(!inactive.physics->body_state(inactive.ref("b"))->active,
            "Inactive spring test did not begin with a sleeping Slider");
    current.distance_spring_frequency = 3;
    current.distance_spring_damping = .2;
    inactive.entity("j").set<PhysicsJoint>(current);
    inactive.physics->synchronize(0);
    require(!inactive.physics->body_state(inactive.ref("b"))->active &&
                inactive.physics->checkpoint()
                        .at("joints")[0]
                        .at("configuration")
                        .at("distance_spring_frequency") == 3,
            "Inactive-kind spring edits woke a body or lost admitted values");

    Fixture disable;
    prepare_distance_spring(disable);
    disable.tick(10);
    current = disable.entity("j").get<PhysicsJoint>();
    require(point(disable, "b")[0] > 1.05,
            "Spring disable test did not begin with an extended elastic link");
    current.distance_spring_frequency = 0;
    disable.entity("j").set<PhysicsJoint>(current);
    disable.tick(60);
    require(std::abs(point(disable, "b")[0] - 1) < 1e-4,
            "Disabling an active spring failed to restore hard distance limits");

    Fixture hard;
    prepare_distance_spring(hard);
    current = hard.entity("j").get<PhysicsJoint>();
    current.distance_spring_frequency = 0;
    hard.entity("j").set<PhysicsJoint>(current);
    hard.tick(60);
    require(std::abs(point(hard, "b")[0] - 1) < 1e-4,
            "Zero spring frequency changed hard distance compatibility");
}
