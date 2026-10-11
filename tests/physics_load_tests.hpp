#pragma once
#include <limits>

Json free_body(double mass, bool sensor = false) {
    auto cube = body("cube", 0, 2);
    cube["components"]["forge.physics_body"]["mass"] = mass;
    cube["components"]["forge.physics_body"]["sensor"] = sensor;
    return {{"version", 1}, {"entities", Json::array({cube})}};
}
void free_world(Fixture& f, double mass, bool sensor = false) {
    auto config = PhysicsConfig{};
    config.gravity = {};
    f.physics->configure(config);
    f.load(free_body(mass, sensor));
}
EntityRef cube_ref(Fixture& f) { return *f.engine.world().reference(f.scene.entity("cube").id()); }
double coordinate(Fixture& f, unsigned axis) {
    const auto p = f.scene.entity("cube").get<LocalTranslation>();
    return axis == 0 ? p.x : axis == 1 ? p.y : p.z;
}
void physics_load_tests() {
    // Exact pinned native default damping is .05/s and applied after integration.
    for (float dt : {1.f / 30, 1.f / 60})
        for (double mass : {1., 2.})
            for (unsigned axis = 0; axis < 3; ++axis)
                for (bool impulse : {false, true}) {
                    Fixture f;
                    free_world(f, mass, axis == 2);
                    Double3 load{};
                    load[axis] = 6;
                    if (impulse)
                        f.physics->add_impulse(cube_ref(f), load);
                    else
                        f.physics->add_force(cube_ref(f), load);
                    check(coordinate(f, axis) == 0, "Queued load changed pose before fixed tick");
                    f.simulation.tick(dt);
                    const double damping = 1 - .05 * double(dt);
                    const double velocity = 6 / mass * (impulse ? 1 : double(dt)) * damping;
                    const auto first = coordinate(f, axis);
                    check(std::abs(first - velocity * dt) < 1e-6,
                          "Force/impulse failed independent mass and fixed-dt equation");
                    check(equivalent(f.scene.entity("cube").get<LocalRotation>(), {}),
                          "Central world load introduced angular motion");
                    f.simulation.tick(dt);
                    check(std::abs(coordinate(f, axis) - first - velocity * damping * dt) < 1e-6,
                          "One-step force/impulse persisted into a later fixed tick");
                }
    for (bool force : {false, true}) {
        Fixture f;
        free_world(f, 2);
        const auto ref = cube_ref(f);
        f.physics->teleport(ref, {10, 20, 30}, {}, true);
        f.tick();
        if (force)
            f.physics->add_force(ref, {0, 6, 0}, LocalTranslation{11, 20, 30});
        else
            f.physics->add_impulse(ref, {0, 6, 0}, LocalTranslation{11, 20, 30});
        f.tick();
        const auto pose = f.scene.entity("cube").get<LocalTranslation>();
        const auto rotation = f.scene.entity("cube").get<LocalRotation>();
        check(pose.y > 20 && std::abs(pose.x - 10) < 1e-6 && std::abs(pose.z - 30) < 1e-6 &&
                  rotation.z > 0 && std::abs(rotation.x) < 1e-6 && std::abs(rotation.y) < 1e-6,
              "Absolute world application point did not produce expected COM torque direction");
    }
    for (bool impulse : {false, true}) {
        Fixture f;
        free_world(f, 2);
        if (impulse)
            f.physics->add_angular_impulse(cube_ref(f), {0, 0, 1});
        else
            f.physics->add_torque(cube_ref(f), {0, 0, 1});
        f.tick();
        check(equivalent(f.scene.entity("cube").get<LocalTranslation>(), {}),
              "Pure torque changed center translation");
        const auto q = f.scene.entity("cube").get<LocalRotation>();
        check(q.z > 0 && std::abs(q.x) < 1e-6 && std::abs(q.y) < 1e-6,
              "World torque/angular impulse did not rotate about its axis");
        const double dt = double(1.f / 60), inertia = 2. / 6;
        const double angle = 1 / inertia * (impulse ? 1 : dt) * (1 - .05 * dt) * dt;
        check(std::abs(q.z - std::sin(angle * .5)) < 1e-6 &&
                  std::abs(q.w - std::cos(angle * .5)) < 1e-6,
              "World torque/angular impulse failed independent box inertia equation");
    }
    for (bool clear_after : {false, true}) {
        Fixture f;
        free_world(f, 2);
        const auto ref = cube_ref(f);
        if (clear_after)
            f.physics->add_impulse(ref, {0, 6, 0});
        f.physics->teleport(ref, {10, 0, 0}, {}, true);
        if (!clear_after)
            f.physics->add_impulse(ref, {0, 6, 0});
        f.tick();
        check(std::abs(coordinate(f, 0) - 10) < 1e-6 && (clear_after ? y(f) == 0 : y(f) > .049),
              "Impulse/clearing teleport order did not remain FIFO");
    }
    {
        // First realization must start before command poses, so an earlier point
        // impulse uses the original COM, not a later teleport's COM.
        Fixture f;
        auto config = PhysicsConfig{};
        config.gravity = {};
        f.physics->configure(config);
        f.scene.restore_snapshot(free_body(2));
        const auto ref = cube_ref(f);
        f.physics->add_impulse(ref, {0, 6, 0}, LocalTranslation{1, 0, 0});
        f.physics->teleport(ref, {10, 0, 0}, {}, false);
        f.tick();
        check(f.scene.entity("cube").get<LocalRotation>().z > 0 && y(f) > .049,
              "New-body realization used a later command's COM before a point impulse");
    }
    {
        Fixture f;
        free_world(f, 2);
        const auto ref = cube_ref(f);
        const auto before = f.physics->status(), snapshot = f.scene.snapshot();
        f.physics->add_force(ref, {0, 6, 0});
        reject([&] { f.physics->synchronize(0); });
        reject([&] { f.physics->synchronize(std::numeric_limits<float>::quiet_NaN()); });
        check(f.scene.snapshot() == snapshot && f.physics->status() == before,
              "Zero/invalid dt consumed a load or mutated state");
        f.tick();
        check(y(f) > 0, "Rejected paused synchronization lost the queued force");
    }
    {
        Fixture original;
        free_world(original, 2);
        const auto ref = cube_ref(original);
        original.physics->add_force(ref, {0, 6, 0}, LocalTranslation{1, 0, 0});
        original.physics->add_impulse(ref, {0, 1, 0});
        original.physics->add_torque(ref, {0, 0, 2});
        original.physics->add_angular_impulse(ref, {0, 0, 1});
        const auto checkpoint = original.physics->checkpoint();
        check(checkpoint.at("version") == 4 && checkpoint.at("commands").size() == 4,
              "Pending loads absent from tagged native recovery");
        Fixture recovered;
        auto config = PhysicsConfig{};
        config.gravity = {};
        recovered.physics->configure(config);
        recovered.scene.restore_snapshot(original.scene.snapshot());
        recovered.physics->restore(checkpoint);
        for (unsigned tick = 0; tick < 30; ++tick) {
            original.tick();
            recovered.tick();
            check(equivalent(original.scene.entity("cube").get<LocalTranslation>(),
                             recovered.scene.entity("cube").get<LocalTranslation>()) &&
                      equivalent(original.scene.entity("cube").get<LocalRotation>(),
                                 recovered.scene.entity("cube").get<LocalRotation>()),
                  "Queued load recovery changed continued native trajectory");
        }
    }
    {
        Fixture f;
        free_world(f, 2);
        const auto ref = cube_ref(f);
        double velocity = 0, expected = 0;
        const double dt = double(1.f / 60), damping = 1 - .05 * dt;
        for (unsigned tick = 0; tick < 30; ++tick) {
            f.physics->add_force(ref, {0, 6, 0});
            f.tick();
            velocity = (velocity + 6 * dt / 2) * damping;
            expected += velocity * dt;
            check(std::abs(y(f) - expected) < 2e-6,
                  "Repeated one-step force failed analytical accumulated motion");
        }
    }
    {
        Fixture f;
        f.load(source());
        f.tick(360);
        const auto ref = cube_ref(f);
        check(f.physics->status().at("active_dynamic") == 0,
              "Zero load fixture did not reach sleeping state");
        f.physics->add_force(ref, {});
        f.physics->add_impulse(ref, {});
        f.physics->add_torque(ref, {});
        f.physics->add_angular_impulse(ref, {});
        f.tick();
        check(f.physics->status().at("active_dynamic") == 0,
              "Zero load unnecessarily woke a sleeping body");
        f.physics->add_impulse(ref, {0, 6000, 0});
        f.tick();
        check(f.physics->status().at("active_dynamic") == 1 && y(f) > .55,
              "Nonzero impulse did not wake and launch a sleeping body");
    }
    {
        Fixture f;
        free_world(f, 2);
        const auto ref = cube_ref(f);
        for (double invalid : {std::numeric_limits<double>::quiet_NaN(),
                               std::numeric_limits<double>::infinity(), 1000001.}) {
            reject([&] { f.physics->add_force(ref, {invalid, 0, 0}); });
            reject([&] { f.physics->add_impulse(ref, {0, invalid, 0}); });
            reject([&] { f.physics->add_torque(ref, {0, 0, invalid}); });
            reject([&] { f.physics->add_angular_impulse(ref, {invalid, 0, 0}); });
            if (!std::isfinite(invalid))
                reject(
                    [&] { f.physics->add_force(ref, {1, 0, 0}, LocalTranslation{invalid, 0, 0}); });
        }
        reject([&] { f.physics->add_force(ref, {1, 0, 0}, LocalTranslation{1000000001., 0, 0}); });
        reject([&] { f.physics->add_force({}, {1, 0, 0}); });
        check(f.physics->checkpoint().at("commands").empty(),
              "Rejected physics loads were appended to the queue");
        f.physics->add_impulse(ref, {0, 6, 0});
        f.physics->teleport(ref, {10, 0, 0}, {}, false);
        auto entity = f.scene.entity("cube");
        const auto original = entity.get<PhysicsBody>();
        auto invalid = original;
        invalid.motion = 0;
        entity.set<PhysicsBody>(invalid);
        const auto state = f.physics->status(), snapshot = f.scene.snapshot();
        reject([&] { f.physics->add_torque(ref, {0, 0, 1}); });
        reject([&] { f.physics->synchronize(1.f / 60); });
        check(f.physics->status() == state && f.scene.snapshot() == snapshot,
              "Invalid load target partially changed native state or later teleport pose");
        entity.set<PhysicsBody>(original);
        f.tick();
        check(std::abs(coordinate(f, 0) - 10) < 1e-6 && y(f) > .049,
              "Rejected load target lost earlier valid FIFO intent");
    }
    {
        Fixture f;
        free_world(f, 2);
        const auto ref = cube_ref(f);
        for (unsigned i = 0; i < 4095; ++i)
            f.physics->add_force(ref, {});
        f.physics->teleport(ref, {1, 0, 0}, {}, true);
        reject([&] { f.physics->add_angular_impulse(ref, {0, 0, 1}); });
        check(f.physics->checkpoint().at("commands").size() == 4096,
              "Load/motion commands do not share the existing capacity bound");
        f.tick();
        check(f.physics->checkpoint().at("commands").empty() &&
                  std::abs(coordinate(f, 0) - 1) < 1e-6,
              "Bounded load queue did not drain once in ordered fixed tick");
    }
    {
        Fixture source;
        free_world(source, 2);
        source.physics->add_force(cube_ref(source), {0, 6, 0});
        const auto checkpoint = source.physics->checkpoint();
        for (unsigned mutation = 0; mutation < 16; ++mutation) {
            auto damaged = checkpoint;
            auto& command = damaged["commands"][0];
            switch (mutation) {
            case 0:
                command["kind"] = 8u;
                break;
            case 1:
                command["kind"] = -1;
                break;
            case 2:
                command["value"][0] = 1000001.;
                break;
            case 3:
                command["clear"] = true;
                break;
            case 4:
                command["at_point"] = true;
                command["kind"] = 4u;
                break;
            case 5:
                command["position"][0] = 1.;
                break;
            case 6:
                command["rotation"] = {0.f, 0.f, .70710677f, .70710677f};
                break;
            case 7:
                command["kind"] = 0u;
                break;
            case 8:
                command["at_point"] = true;
                command["position"][0] = 1000000001.;
                break;
            case 9:
                command["kind"] = (std::uint64_t{1} << 32) + 2;
                break;
            case 10:
                command["position"].push_back(0.);
                break;
            case 11:
                command["rotation"].push_back(0.);
                break;
            case 12:
                command["value"].push_back(0.);
                break;
            case 13:
                command["kind"] = std::numeric_limits<std::uint64_t>::max();
                break;
            case 14:
                command["kind"] = 2.;
                break;
            case 15:
                command["kind"] = 1e300;
                break;
            }
            // Re-seal transport corruption detection to exercise command admission,
            // rather than letting the outer checksum mask a malformed command.
            damaged.erase("integrity");
            std::uint64_t checksum = 14695981039346656037ull;
            for (unsigned char byte : damaged.dump()) {
                checksum ^= byte;
                checksum *= 1099511628211ull;
            }
            damaged["integrity"] = std::to_string(checksum);
            Fixture target;
            auto config = PhysicsConfig{};
            config.gravity = {};
            target.physics->configure(config);
            target.scene.restore_snapshot(source.scene.snapshot());
            const auto before = target.scene.snapshot(), state = target.physics->status();
            reject([&] { target.physics->restore(damaged); });
            check(target.scene.snapshot() == before && target.physics->status() == state,
                  "Malformed tagged recovery command partially reconstructed native state");
            target.physics->restore(checkpoint);
            target.tick();
            check(y(target) > 0, "Rejected recovery command damaged valid fresh restoration");
        }
    }
}
