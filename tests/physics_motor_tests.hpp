#pragma once
void prepare_motor_fixture(Fixture& f, unsigned kind, bool dynamic_support = false) {
    f.setup();
    auto joint = f.entity("j").get<PhysicsJoint>();
    joint.kind = kind;
    joint.axis_a = joint.axis_b = kind == 1 ? 2 : 0;
    joint.normal_a = joint.normal_b = kind == 1 ? 0 : 1;
    joint.min_travel = -20;
    joint.max_travel = 20;
    joint.min_angle = -170;
    joint.max_angle = 170;
    joint.motor_enabled = true;
    joint.hinge_motor_speed = 90;
    joint.slider_motor_speed = 3;
    joint.max_motor_torque = .06f;
    joint.max_motor_force = 1;
    f.entity("j").set<PhysicsJoint>(joint);
    f.entity("b").set<LocalTranslation>({0, 4, 0});
    for (const auto* name : {"a", "b"}) {
        auto body = f.entity(name).get<PhysicsBody>();
        body.mask = 0;
        body.mass = 2;
        body.gravity_factor = body.linear_damping = body.angular_damping = 0;
        body.allow_sleeping = false;
        if (dynamic_support && std::string_view(name) == "a")
            body.motion = 2;
        f.entity(name).set<PhysicsBody>(body);
        f.entity(name).set<BoxCollider>({.6f, .6f, .6f});
    }
    f.physics->synchronize(0);
}
double motor_speed(Fixture& f, unsigned kind, const char* name = "b") {
    const auto state = f.physics->body_state(f.ref(name));
    require(bool(state), "Motor body native state absent");
    return kind == 1 ? state->angular_velocity[2] : state->linear_velocity[0];
}
void joint_velocity_motors() {
    // Slider F/m = 1/2; hinge torque/I = .06/.12 for a .6m cube of mass2.
    // All are independent native body observations, not authored target readback.
    for (unsigned kind : {1u, 4u}) {
        Fixture f;
        prepare_motor_fixture(f, kind);
        f.tick(60);
        const double dt = double(1.f / 60), acceleration = .5;
        const double expected_speed = acceleration * dt * 60;
        const double expected_displacement = acceleration * dt * dt * (60 * 61 / 2);
        require(std::abs(motor_speed(f, kind) - expected_speed) < 1e-4,
                "Force/torque limited motor acceleration disagrees with independent inertia");
        const auto q = f.entity("b").get<LocalRotation>();
        const double displacement = kind == 1 ? 2 * std::atan2(q.z, q.w) : point(f, "b")[0];
        require(std::abs(displacement - expected_displacement) < 1e-4,
                "Motor displacement differs from fixed-step integration");
        auto joint = f.entity("j").get<PhysicsJoint>();
        joint.motor_enabled = false;
        f.entity("j").set<PhysicsJoint>(joint);
        rejects([&] { (void)f.physics->checkpoint(); });
        f.tick(10);
        require(std::abs(motor_speed(f, kind) - expected_speed) < 1e-4,
                "Disabled velocity motor failed to coast");
        joint.motor_enabled = true;
        joint.hinge_motor_speed = -90;
        joint.slider_motor_speed = -1;
        joint.max_motor_force = joint.max_motor_torque = 0;
        f.entity("j").set<PhysicsJoint>(joint);
        f.tick(10);
        require(std::abs(motor_speed(f, kind) - expected_speed) < 1e-4,
                "Zero effort motor changed native velocity");
        // Reversal uses bounded acceleration, then reaches the negative target.
        joint.max_motor_force = 4;
        joint.max_motor_torque = .24f;
        f.entity("j").set<PhysicsJoint>(joint);
        f.tick(20);
        const auto snapshot = f.scene.snapshot(), checkpoint = f.physics->checkpoint();
        Fixture recovered;
        recovered.scene.restore_snapshot(snapshot);
        recovered.physics->restore(checkpoint);
        for (unsigned i = 0; i < 60; ++i) {
            f.tick();
            recovered.tick();
            require(distance(point(f, "b"), point(recovered, "b")) < 1e-5 &&
                        std::abs(motor_speed(f, kind) - motor_speed(recovered, kind)) < 1e-5 &&
                        distance(direction(f, "b", {1, 0, 0}),
                                 direction(recovered, "b", {1, 0, 0})) < 1e-5,
                    "Fresh motor recovery lost native effort/target/solver trajectory");
        }
        const double target = kind == 1 ? -std::numbers::pi / 2 : -1;
        require(std::abs(motor_speed(f, kind) - target) < 1e-4,
                "Velocity motor did not reach its signed relative target");
        joint.hinge_motor_speed = joint.slider_motor_speed = 0;
        f.entity("j").set<PhysicsJoint>(joint);
        f.tick(60);
        require(std::abs(motor_speed(f, kind)) < 1e-4,
                "Zero target with finite effort failed to brake");
        // Both Dynamic bodies must receive opposite efforts; a velocity motor
        // drives relative speed, not an absolute velocity on Body B alone.
        Fixture pair;
        prepare_motor_fixture(pair, kind, true);
        pair.tick(60);
        require(std::abs(motor_speed(pair, kind, "a") + expected_speed) < 1e-4 &&
                    std::abs(motor_speed(pair, kind) - expected_speed) < 1e-4,
                "Motor ignored reaction effort on Dynamic support");
    }
    Fixture order;
    prepare_motor_fixture(order, 4);
    const auto motor = order.entity("j").get<PhysicsJoint>();
    auto attachment = motor;
    attachment.kind = 2;
    order.entity("j2").set<PhysicsJoint>(attachment);
    attachment.kind = 3;
    order.entity("j3").set<PhysicsJoint>(attachment);
    order.physics->synchronize(0);
    const auto native_order = [](const Json& checkpoint) {
        Json ids = Json::array();
        for (const auto& item : checkpoint.at("joints"))
            ids.push_back(item.at("entity"));
        return ids;
    };
    const auto before = order.physics->checkpoint();
    require(before.at("joints").size() == 3 &&
                before.at("joints")[0].at("entity") == Json(order.ref("j")),
            "Motor update test did not exercise a nonlast native constraint");
    auto updated = motor;
    updated.slider_motor_speed = -2;
    updated.max_motor_force = 8;
    order.entity("j").set<PhysicsJoint>(updated);
    order.physics->synchronize(0);
    require(native_order(order.physics->checkpoint()) == native_order(before),
            "Motor-only change retired the constraint and reordered native manager state");
    const auto admitted = order.physics->checkpoint();
    for (unsigned mode = 0; mode < 5; ++mode) {
        auto invalid = updated;
        if (mode == 0)
            invalid.max_motor_force = -1;
        else if (mode == 1)
            invalid.max_motor_torque = std::numeric_limits<float>::infinity();
        else if (mode == 2)
            invalid.slider_motor_speed = 1001;
        else if (mode == 3)
            invalid.hinge_motor_speed = -36001;
        else
            invalid.slider_motor_speed = std::numeric_limits<float>::quiet_NaN();
        order.entity("j").set<PhysicsJoint>(invalid);
        rejects([&] { order.physics->synchronize(0); });
        order.entity("j").set<PhysicsJoint>(updated);
        require(order.physics->checkpoint() == admitted,
                "Invalid motor candidate changed admitted native state before rejection");
    }
    auto legacy = physics_detail::joint_value(updated);
    for (const auto* field : {"motor_enabled", "hinge_motor_speed", "slider_motor_speed",
                              "max_motor_force", "max_motor_torque"})
        legacy.erase(field);
    const auto defaults = physics_detail::joint_from_value(legacy);
    require(!defaults.motor_enabled && defaults.hinge_motor_speed == 90 &&
                defaults.slider_motor_speed == 1 && defaults.max_motor_force == 100 &&
                defaults.max_motor_torque == 10,
            "Legacy joint codec failed to preserve disabled motor defaults");
    auto legacy_scene = order.scene.snapshot();
    for (auto& row : legacy_scene["entities"])
        if (auto it = row["components"].find("forge.physics_joint"); it != row["components"].end())
            for (const auto* field : {"motor_enabled", "hinge_motor_speed", "slider_motor_speed",
                                      "max_motor_force", "max_motor_torque"})
                it->erase(field);
    Fixture old_scene;
    old_scene.scene.restore_snapshot(legacy_scene);
    require(old_scene.entity("j").get<PhysicsJoint>() == defaults,
            "Legacy Scene admission failed to backfill disabled drive controls");
    Fixture sleeping;
    prepare_motor_fixture(sleeping, 4);
    auto asleep = sleeping.entity("b").get<PhysicsBody>();
    asleep.allow_sleeping = true;
    sleeping.entity("b").set<PhysicsBody>(asleep);
    auto drive = sleeping.entity("j").get<PhysicsJoint>();
    drive.motor_enabled = false;
    sleeping.entity("j").set<PhysicsJoint>(drive);
    sleeping.tick(180);
    require(!sleeping.physics->body_state(sleeping.ref("b"))->active,
            "Motor wake test did not begin with a sleeping carriage");
    drive.hinge_motor_speed = -45; // Inactive hinge data must not wake a Slider.
    sleeping.entity("j").set<PhysicsJoint>(drive);
    sleeping.physics->synchronize(0);
    require(!sleeping.physics->body_state(sleeping.ref("b"))->active,
            "Inactive motor field edit woke a sleeping body");
    drive.motor_enabled = true;
    drive.slider_motor_speed = -1;
    sleeping.entity("j").set<PhysicsJoint>(drive);
    sleeping.tick();
    require(sleeping.physics->body_state(sleeping.ref("b"))->active && motor_speed(sleeping, 4) < 0,
            "Enabled motor failed to wake and drive a sleeping body");
}
