#pragma once
void slider_motion_and_recovery() {
    // Known lower/upper endpoints on each principal axis, a rotated frame and
    // signed/scaled local anchors. Transverse velocity and rotation must lock.
    for (unsigned mode = 0; mode < 6; ++mode)
        for (int sign : {-1, 1}) {
            Fixture f;
            f.setup();
            const unsigned axis = mode < 3 ? mode : 0;
            PhysicsJoint joint = f.entity("j").get<PhysicsJoint>();
            joint.kind = 4;
            joint.axis_a = joint.axis_b = axis;
            joint.normal_a = joint.normal_b = (axis + 1) % 3;
            joint.min_travel = -.75f;
            joint.max_travel = 1.25f;
            f.entity("b").set<LocalTranslation>({0, 4, 0});
            if (mode == 3) {
                const LocalRotation q{0, 0, float(std::sin(std::numbers::pi / 4)),
                                      float(std::cos(std::numbers::pi / 4))};
                f.entity("a").set<LocalRotation>(q);
                f.entity("b").set<LocalRotation>(q);
            }
            if (mode == 4) {
                f.entity("a").set<LocalScale>({-2, 1, 1});
                f.entity("b").set<LocalScale>({.5f, 2, 1});
                joint.anchor_a_x = .25f;
                joint.anchor_b_x = 1;
                f.entity("b").set<LocalTranslation>({-1, 4, 0});
            }
            if (mode == 5) {
                const auto id = authoring_command(f.scene, "entity.create", {{"name", "Parent"}})
                                    .at("selected")
                                    .get<std::string>();
                auto parent = f.scene.entity(id);
                parent.set<LocalTranslation>({10, 2, -1});
                parent.set<LocalRotation>({0, 0, std::sqrt(.5f), std::sqrt(.5f)});
                parent.set<LocalScale>({2, 2, 2});
                f.entity("a")
                    .add(flecs::ChildOf, parent)
                    .set<SpatialBinding>({SpatialMode::FollowStructure, {}});
                // Dynamic bodies use World child space under structural parents.
                f.entity("b")
                    .add(flecs::ChildOf, parent)
                    .set<SpatialBinding>({SpatialMode::World, {}})
                    .set<LocalTranslation>({2, 2, -1})
                    .set<LocalRotation>({0, 0, std::sqrt(.5f), std::sqrt(.5f)});
            }
            for (const auto* name : {"a", "b"}) {
                auto body = f.entity(name).get<PhysicsBody>();
                body.mask = 0;
                body.gravity_factor = body.linear_damping = body.angular_damping = 0;
                body.allow_sleeping = false;
                f.entity(name).set<PhysicsBody>(body);
            }
            f.entity("j").set<PhysicsJoint>(joint);
            f.physics->synchronize(0);
            const auto start = point(f, "b");
            Double3 frame_axis{};
            frame_axis[axis] = 1;
            if (mode == 3 || mode == 5)
                frame_axis = {0, 1, 0};
            Double3 velocity{};
            for (unsigned i = 0; i < 3; ++i)
                velocity[i] = sign * 10 * frame_axis[i];
            velocity[(axis + 2) % 3] += 3;
            const auto orientation = direction(f, "b", {1, 0, 0});
            const auto normal = direction(f, "b", {0, 1, 0});
            f.physics->set_linear_velocity(f.ref("b"), velocity);
            f.physics->set_angular_velocity(f.ref("b"), {2, 3, 4});
            f.tick(20);
            const auto snapshot = f.scene.snapshot(), checkpoint = f.physics->checkpoint();
            Fixture recovered;
            recovered.scene.restore_snapshot(snapshot);
            recovered.physics->restore(checkpoint);
            for (unsigned i = 0; i < 70; ++i) {
                f.tick();
                recovered.tick();
                require(distance(point(f, "b"), point(recovered, "b")) < 1e-5 &&
                            distance(direction(f, "b", {1, 0, 0}),
                                     direction(recovered, "b", {1, 0, 0})) < 1e-5 &&
                            distance(direction(f, "b", {0, 1, 0}),
                                     direction(recovered, "b", {0, 1, 0})) < 1e-5,
                        "Slider native recovery lost trajectory or frame");
            }
            auto expected = start;
            const auto limit = sign < 0 ? joint.min_travel : joint.max_travel;
            for (unsigned i = 0; i < 3; ++i)
                expected[i] += frame_axis[i] * limit;
            require(
                distance(point(f, "b"), expected) < .003 &&
                    distance(direction(f, "b", {1, 0, 0}), orientation) < .002 &&
                    distance(direction(f, "b", {0, 1, 0}), normal) < .002,
                "Slider endpoint, transverse locking or rotation frame disagrees with geometry");
            const auto state = f.physics->body_state(f.ref("b"));
            require(state && std::abs(state->linear_velocity[0]) < .001 &&
                        std::abs(state->linear_velocity[1]) < .001 &&
                        std::abs(state->linear_velocity[2]) < .001,
                    "Slider endpoint retained outward native velocity");
        }
    Fixture f;
    f.setup();
    auto joint = f.entity("j").get<PhysicsJoint>();
    // Original documents omit the new fields and preserve their old settings.
    auto legacy = physics_detail::joint_value(joint);
    legacy.erase("min_travel");
    legacy.erase("max_travel");
    const auto restored = physics_detail::joint_from_value(legacy);
    require(restored == joint, "Legacy joint document changed with defaulted travel fields");
    auto legacy_scene = f.scene.snapshot();
    for (auto& row : legacy_scene["entities"])
        if (auto it = row["components"].find("forge.physics_joint");
            it != row["components"].end()) {
            it->erase("min_travel");
            it->erase("max_travel");
        }
    Fixture old_document;
    old_document.scene.restore_snapshot(legacy_scene);
    require(old_document.entity("j").get<PhysicsJoint>() == joint,
            "Legacy joint scene admission changed defaults or rejected missing travel fields");
    joint.kind = 4;
    f.entity("j").set<PhysicsJoint>(joint);
    f.physics->synchronize(0);
    const auto before = f.physics->checkpoint();
    for (unsigned mode = 0; mode < 5; ++mode) {
        auto invalid = joint;
        if (mode == 0)
            invalid.min_travel = .1f;
        else if (mode == 1)
            invalid.max_travel = -.1f;
        else if (mode == 2)
            invalid.min_travel = invalid.max_travel = 0;
        else if (mode == 3)
            invalid.normal_a = invalid.axis_a;
        else
            invalid.max_travel = std::numeric_limits<float>::infinity();
        f.entity("j").set<PhysicsJoint>(invalid);
        rejects([&] { f.physics->synchronize(1.f / 60); });
        f.entity("j").set<PhysicsJoint>(joint);
        require(f.physics->checkpoint() == before, "Rejected slider mutated native state");
    }
}
