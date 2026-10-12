#pragma once
#include <memory>
double motor_position(Fixture& f, unsigned kind) {
    if (kind == 4)
        return point(f, "b")[0];
    const auto q = f.entity("b").get<LocalRotation>();
    return 2 * std::atan2(q.z, q.w);
}
void joint_position_motors() {
    for (unsigned kind : {1u, 4u}) {
        for (float damping : {1.f, .15f}) {
            Fixture f;
            prepare_motor_fixture(f, kind);
            auto drive = f.entity("j").get<PhysicsJoint>();
            drive.motor_mode = 1;
            drive.hinge_motor_angle = 30;
            drive.slider_motor_position = .5f;
            drive.motor_frequency = 1;
            drive.motor_damping = damping;
            drive.max_motor_force = drive.max_motor_torque = 1000;
            f.entity("j").set<PhysicsJoint>(drive);
            const double target = kind == 1 ? std::numbers::pi / 6 : .5;
            const double dt = double(1.f / 60), omega = 2 * std::numbers::pi;
            const double divisor = 1 + 2 * damping * omega * dt + omega * omega * dt * dt;
            double position = 0, velocity = 0, maximum = 0;
            std::unique_ptr<Fixture> recovered;
            for (unsigned i = 0; i < 120; ++i) {
                // Independent implicit Euler solution of a normalized spring.
                velocity = (velocity + dt * omega * omega * (target - position)) / divisor;
                position += dt * velocity;
                f.tick();
                require(std::abs(motor_position(f, kind) - position) < 1e-4 &&
                            std::abs(motor_speed(f, kind) - velocity) < 1e-4,
                        "Native position motor differs from independent spring integration");
                maximum = std::max(maximum, motor_position(f, kind));
                if (recovered) {
                    recovered->tick();
                    require(std::abs(motor_position(f, kind) - motor_position(*recovered, kind)) <
                                    1e-5 &&
                                std::abs(motor_speed(f, kind) - motor_speed(*recovered, kind)) <
                                    1e-5,
                            "Fresh position motor recovery changed its spring trajectory");
                }
                if (i == 19) {
                    recovered = std::make_unique<Fixture>();
                    recovered->scene.restore_snapshot(f.scene.snapshot());
                    recovered->physics->restore(f.physics->checkpoint());
                }
            }
            require(damping < 1 ? maximum > target + .05
                                : maximum <= target + 1e-4 &&
                                      std::abs(motor_position(f, kind) - target) < 3e-4,
                    "Position damping failed to distinguish overshoot and critical return");
        }
        Fixture limited;
        prepare_motor_fixture(limited, kind, true);
        auto drive = limited.entity("j").get<PhysicsJoint>();
        drive.motor_mode = 1;
        drive.hinge_motor_angle = 160;
        drive.slider_motor_position = 10;
        limited.entity("j").set<PhysicsJoint>(drive);
        limited.tick();
        const double expected = .5 * double(1.f / 60);
        require(std::abs(motor_speed(limited, kind) - expected) < 1e-5 &&
                    std::abs(motor_speed(limited, kind, "a") + expected) < 1e-5,
                "Position drive ignored effort/inertia or Dynamic reaction");
        const auto admitted = limited.physics->checkpoint();
        const auto native_order = admitted.at("joints");
        for (unsigned mode = 0; mode < 6; ++mode) {
            auto invalid = drive;
            if (mode == 0)
                invalid.motor_frequency = 0;
            else if (mode == 1)
                invalid.motor_damping = 11;
            else if (mode == 2)
                invalid.motor_mode = 2;
            else if (mode == 3)
                invalid.motor_frequency = std::numeric_limits<float>::quiet_NaN();
            else if (mode == 4) {
                invalid.hinge_motor_angle = 171;
                invalid.slider_motor_position = 21;
            } else {
                invalid.max_angle = 90;
                invalid.max_travel = 1;
            }
            limited.entity("j").set<PhysicsJoint>(invalid);
            rejects([&] { limited.physics->synchronize(0); });
            limited.entity("j").set<PhysicsJoint>(drive);
            require(limited.physics->checkpoint() == admitted,
                    "Rejected spring candidate mutated admitted solver state");
        }
        drive.motor_mode = 0;
        drive.hinge_motor_speed = -30;
        drive.slider_motor_speed = -.5f;
        drive.max_motor_force = 4;
        drive.max_motor_torque = .24f;
        limited.entity("j").set<PhysicsJoint>(drive);
        limited.physics->synchronize(0);
        const auto changed = limited.physics->checkpoint();
        require(changed.at("joints").size() == native_order.size() &&
                    changed.at("joints")[0].at("entity") == native_order[0].at("entity"),
                "Position/velocity mode change retired the native constraint");
        limited.tick(30);
        const double relative = motor_speed(limited, kind) - motor_speed(limited, kind, "a");
        require(std::abs(relative - (kind == 1 ? -std::numbers::pi / 6 : -.5)) < 1e-4,
                "Changed position motor failed to adopt signed velocity mode");
    }
    Fixture asleep;
    prepare_motor_fixture(asleep, 4);
    auto body = asleep.entity("b").get<PhysicsBody>();
    body.allow_sleeping = true;
    asleep.entity("b").set<PhysicsBody>(body);
    auto drive = asleep.entity("j").get<PhysicsJoint>();
    drive.motor_enabled = false;
    drive.motor_mode = 1;
    asleep.entity("j").set<PhysicsJoint>(drive);
    asleep.tick(180);
    require(!asleep.physics->body_state(asleep.ref("b"))->active,
            "Position motor wake fixture failed to sleep");
    drive.slider_motor_speed = -2; // Inactive mode must not wake a position drive.
    asleep.entity("j").set<PhysicsJoint>(drive);
    asleep.physics->synchronize(0);
    require(!asleep.physics->body_state(asleep.ref("b"))->active,
            "Inactive velocity target woke a position-mode body");
    drive.slider_motor_position = -.5f;
    drive.motor_enabled = true;
    asleep.entity("j").set<PhysicsJoint>(drive);
    asleep.tick();
    require(asleep.physics->body_state(asleep.ref("b"))->active && motor_speed(asleep, 4) < 0,
            "Position drive failed to wake and move a sleeping body");
    auto legacy = physics_detail::joint_value(drive);
    for (const auto* field : {"motor_mode", "hinge_motor_angle", "slider_motor_position",
                              "motor_frequency", "motor_damping"})
        legacy.erase(field);
    const auto restored = physics_detail::joint_from_value(legacy);
    require(restored.motor_mode == 0 && restored.motor_frequency == 2 &&
                restored.motor_damping == 1 && restored.hinge_motor_angle == 0 &&
                restored.slider_motor_position == 0,
            "Legacy velocity motor lost position defaults");
    Fixture order;
    prepare_motor_fixture(order, 4);
    auto support = order.entity("j").get<PhysicsJoint>();
    support.kind = 3;
    order.entity("j2").set<PhysicsJoint>(support);
    order.entity("j3").set<PhysicsJoint>(support);
    order.physics->synchronize(0);
    auto configured = order.entity("j").get<PhysicsJoint>();
    const auto before = order.physics->checkpoint();
    require(before.at("joints").size() == 3 &&
                before.at("joints")[0].at("entity") == Json(order.ref("j")),
            "Position retention fixture did not exercise a nonlast constraint");
    configured.motor_mode = 1;
    configured.slider_motor_position = -.5f;
    configured.motor_frequency = 1.5f;
    configured.motor_damping = .7f;
    order.entity("j").set<PhysicsJoint>(configured);
    order.physics->synchronize(0);
    const auto after = order.physics->checkpoint();
    for (unsigned i = 0; i < 3; ++i)
        require(before.at("joints")[i].at("entity") == after.at("joints")[i].at("entity"),
                "Position target/mode/spring edit reordered native constraints");
}
