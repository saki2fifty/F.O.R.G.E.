#pragma once
#include "physics_load_tests.hpp"
#include <numbers>

Json reseal_velocity_checkpoint(Json value) {
    value.erase("integrity");
    std::uint64_t checksum = 14695981039346656037ull;
    for (unsigned char byte : value.dump()) {
        checksum ^= byte;
        checksum *= 1099511628211ull;
    }
    value["integrity"] = std::to_string(checksum);
    return value;
}
void physics_velocity_tests() {
    for (float dt : {1.f / 30, 1.f / 60})
        for (double mass : {1., 2.})
            for (unsigned axis = 0; axis < 3; ++axis) {
                Fixture f;
                free_world(f, mass);
                const auto ref = cube_ref(f);
                const auto initial = f.physics->body_state(ref);
                Double3 linear{}, angular{};
                linear[axis] = 3;
                angular[axis] = 2;
                f.physics->set_linear_velocity(ref, linear);
                f.physics->set_angular_velocity(ref, angular);
                check(f.physics->body_state(ref) == initial,
                      "Velocity setter wrote native state before the fixed boundary");
                const double damping = 1 - .05 * double(dt);
                f.simulation.tick(dt);
                auto state = f.physics->body_state(ref);
                check(state && std::abs(state->linear_velocity[axis] - 3 * damping) < 1e-6 &&
                          std::abs(state->angular_velocity[axis] - 2 * damping) < 1e-6 &&
                          std::abs(state->centre_of_mass[axis] - 3 * damping * dt) < 1e-6,
                      "Velocity setter failed independent mass-free linear/angular equation");
                const auto pose = state->centre_of_mass;
                f.physics->set_linear_velocity(ref, {});
                f.simulation.tick(dt);
                state = f.physics->body_state(ref);
                check(state && state->linear_velocity == Double3{} &&
                          state->centre_of_mass == pose &&
                          std::abs(state->angular_velocity[axis] - 2 * damping * damping) < 1e-6,
                      "Zero linear command failed to stop motion or erased angular velocity");
                linear[axis] = 4;
                f.physics->set_linear_velocity(ref, linear);
                f.physics->set_angular_velocity(ref, {});
                f.simulation.tick(dt);
                state = f.physics->body_state(ref);
                check(state && std::abs(state->linear_velocity[axis] - 4 * damping) < 1e-6 &&
                          state->angular_velocity == Double3{},
                      "Zero angular command failed to stop rotation or erased linear velocity");
            }
    for (bool angular : {false, true})
        for (bool setter_first : {false, true}) {
            Fixture f;
            free_world(f, 2);
            const auto ref = cube_ref(f);
            auto entity = f.scene.entity("cube");
            auto settings = entity.get<PhysicsBody>();
            settings.linear_damping = settings.angular_damping = 0;
            entity.set<PhysicsBody>(settings);
            f.physics->synchronize(0);
            const auto velocity = [&] {
                if (angular)
                    f.physics->set_angular_velocity(ref, {0, 0, 2});
                else
                    f.physics->set_linear_velocity(ref, {3, 0, 0});
            };
            const auto load = [&] {
                if (angular)
                    f.physics->add_torque(ref, {0, 0, 1});
                else
                    f.physics->add_force(ref, {4, 0, 0});
            };
            if (setter_first) {
                velocity();
                load();
            } else {
                load();
                velocity();
            }
            f.tick();
            const auto state = f.physics->body_state(ref);
            const double dt = double(1.f / 60);
            const double expected = angular ? 2 + 3 * dt : 3 + 2 * dt;
            check(state &&
                      std::abs((angular ? state->angular_velocity[2] : state->linear_velocity[0]) -
                               expected) < 1e-6,
                  "Velocity setter erased a force/torque accumulated for native integration");
        }
    {
        Fixture f;
        free_world(f, 2);
        const auto ref = cube_ref(f);
        auto entity = f.scene.entity("cube");
        auto config = entity.get<PhysicsBody>();
        config.linear_damping = config.angular_damping = 0;
        config.max_linear_velocity = 2;
        config.max_angular_velocity = 90;
        entity.set<PhysicsBody>(config);
        f.physics->synchronize(0);
        f.physics->set_linear_velocity(ref, {3, 4, 0});
        f.physics->set_angular_velocity(ref, {0, 3, 4});
        f.tick();
        const auto state = f.physics->body_state(ref);
        check(state && std::abs(state->linear_velocity[0] - 1.2) < 1e-6 &&
                  std::abs(state->linear_velocity[1] - 1.6) < 1e-6 &&
                  std::abs(state->angular_velocity[1] - std::numbers::pi * .3) < 1e-6 &&
                  std::abs(state->angular_velocity[2] - std::numbers::pi * .4) < 1e-6,
              "Velocity setter bypassed native vector magnitude caps or radians units");
        config.mass = 4;
        entity.set<PhysicsBody>(config);
        f.physics->set_linear_velocity(ref, {0, 1, 0});
        f.tick();
        const auto replaced = f.physics->body_state(ref);
        check(replaced && replaced->linear_velocity == Double3{0, 1, 0} &&
                  replaced->angular_velocity == state->angular_velocity,
              "Pending setter/replacement lost compatible opposite velocity channel");
    }
    for (unsigned ordering = 0; ordering < 6; ++ordering) {
        Fixture f;
        free_world(f, 2);
        const auto ref = cube_ref(f);
        auto entity = f.scene.entity("cube");
        auto settings = entity.get<PhysicsBody>();
        settings.linear_damping = settings.angular_damping = 0;
        entity.set<PhysicsBody>(settings);
        f.physics->synchronize(0);
        const bool angular = ordering >= 4;
        const auto velocity = [&] {
            if (angular)
                f.physics->set_angular_velocity(ref, {0, 0, 2});
            else
                f.physics->set_linear_velocity(ref, {3, 0, 0});
        };
        const auto other = [&] {
            if (angular)
                f.physics->add_angular_impulse(ref, {0, 0, 1});
            else if (ordering >= 2)
                f.physics->teleport(ref, {}, {}, true);
            else
                f.physics->add_impulse(ref, {4, 0, 0});
        };
        if (ordering % 2 == 0) {
            velocity();
            other();
        } else {
            other();
            velocity();
        }
        f.tick();
        const auto state = f.physics->body_state(ref);
        const double expected[] = {5, 3, 0, 3, 5, 2};
        check(state && std::abs((angular ? state->angular_velocity[2] : state->linear_velocity[0]) -
                                expected[ordering]) < 1e-6,
              "Velocity command order differs from impulses or clearing teleport FIFO");
    }
    {
        Fixture f;
        free_world(f, 1);
        const auto ref = cube_ref(f);
        f.tick(360);
        check(!f.physics->body_state(ref)->active, "Velocity sleep test precondition missing");
        f.physics->set_linear_velocity(ref, {});
        f.physics->set_angular_velocity(ref, {});
        f.tick();
        check(!f.physics->body_state(ref)->active,
              "Zero velocity commands unnecessarily woke a native sleeping body");
        f.physics->set_angular_velocity(ref, {0, 1, 0});
        f.tick();
        check(f.physics->body_state(ref)->active &&
                  f.physics->body_state(ref)->angular_velocity[1] > .99,
              "Angular velocity request failed native sleeper activation");
        f.physics->teleport(ref, {}, {}, true);
        f.tick(360);
        auto entity = f.scene.entity("cube");
        auto settings = entity.get<PhysicsBody>();
        settings.max_linear_velocity = 0;
        entity.set<PhysicsBody>(settings);
        f.physics->synchronize(0);
        check(!f.physics->body_state(ref)->active, "Zero-cap replacement did not preserve sleep");
        f.physics->set_linear_velocity(ref, {5, 0, 0});
        f.tick();
        const auto state = f.physics->body_state(ref);
        check(state && state->active && state->linear_velocity == Double3{},
              "Zero cap bypassed native request-based activation or velocity clamp");
    }
    {
        Fixture f;
        free_world(f, 2);
        const auto ref = cube_ref(f);
        f.physics->set_linear_velocity(ref, {2, 0, 0});
        const auto before = f.physics->checkpoint();
        for (double invalid : {1e6 + 1, std::numeric_limits<double>::infinity(),
                               std::numeric_limits<double>::quiet_NaN()}) {
            reject([&] { f.physics->set_linear_velocity(ref, {invalid, 0, 0}); });
            reject([&] { f.physics->set_angular_velocity(ref, {0, invalid, 0}); });
        }
        reject([&] {
            f.physics->set_linear_velocity({AssetId::generate(), EntityId::generate()}, {});
        });
        reject([&] { f.physics->synchronize(0); });
        bool linear_rejected = false, angular_rejected = false;
        std::thread foreign([&] {
            try {
                f.physics->set_linear_velocity(ref, {});
            } catch (const std::exception&) {
                linear_rejected = true;
            }
            try {
                f.physics->set_angular_velocity(ref, {});
            } catch (const std::exception&) {
                angular_rejected = true;
            }
        });
        foreign.join();
        check(linear_rejected && angular_rejected && f.physics->checkpoint() == before,
              "Invalid velocity command mutated native state or pending FIFO");
        auto entity = f.scene.entity("cube");
        const auto original = entity.get<PhysicsBody>();
        auto changed = original;
        changed.motion = 0;
        entity.set<PhysicsBody>(changed);
        reject([&] { f.physics->synchronize(1.f / 60); });
        entity.set<PhysicsBody>(original);
        check(f.physics->checkpoint() == before,
              "Pending motion-mode rejection partially applied velocity intent");
        f.tick();
    }
    for (unsigned mode = 0; mode < 3; ++mode) {
        Fixture f;
        auto scene = free_body(1);
        auto& settings = scene["entities"][0]["components"]["forge.physics_body"];
        settings["motion"] = mode;
        settings["enabled"] = mode != 2;
        f.load(scene);
        const auto ref = cube_ref(f);
        reject([&] { f.physics->set_linear_velocity(ref, {}); });
        reject([&] { f.physics->set_angular_velocity(ref, {}); });
    }
    {
        Fixture f;
        free_world(f, 1);
        const auto ref = cube_ref(f);
        for (unsigned i = 0; i < 4096; ++i)
            f.physics->set_linear_velocity(ref, {});
        check(f.physics->checkpoint().at("commands").size() == 4096,
              "Zero velocity commands bypassed the shared queue bound");
        reject([&] { f.physics->set_angular_velocity(ref, {}); });
        f.tick();
        check(f.physics->checkpoint().at("commands").empty(),
              "Bounded velocity queue failed to drain at its fixed boundary");
    }
    {
        Fixture original;
        original.physics->configure(PhysicsConfig{{0, 0, 0}});
        original.scene.restore_snapshot(free_body(2));
        const auto ref = cube_ref(original);
        original.physics->set_linear_velocity(ref, {3, 0, 0});
        original.physics->set_angular_velocity(ref, {0, 0, 2});
        check(!original.physics->body_state(ref), "Initial velocity request realized a body early");
        original.tick();
        original.physics->add_force(ref, {3, 0, 0});
        original.physics->add_impulse(ref, {0, 0, 4});
        original.physics->add_torque(ref, {0, 1, 0});
        original.physics->add_angular_impulse(ref, {1, 0, 0});
        original.physics->set_linear_velocity(ref, {3, 1, 0});
        original.physics->set_angular_velocity(ref, {0, 0, 2});
        const auto checkpoint = original.physics->checkpoint();
        check(checkpoint.at("version") == 5 && checkpoint.at("commands").size() == 6 &&
                  checkpoint.at("commands")[4].at("kind") == 6 &&
                  checkpoint.at("commands")[5].at("kind") == 7,
              "Velocity pending intent absent from strict version5 recovery");
        Fixture recovered;
        recovered.physics->configure(PhysicsConfig{{0, 0, 0}});
        recovered.scene.restore_snapshot(original.scene.snapshot());
        auto obsolete = checkpoint;
        obsolete["version"] = 3;
        reject([&] { recovered.physics->restore(reseal_velocity_checkpoint(obsolete)); });
        recovered.physics->restore(checkpoint);
        for (unsigned i = 0; i < 30; ++i) {
            original.tick();
            recovered.tick();
            check(original.physics->body_state(ref) == recovered.physics->body_state(ref),
                  "Fresh version4 recovery changed pending velocity/load continued state");
        }
    }
}
