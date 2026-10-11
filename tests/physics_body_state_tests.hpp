#pragma once
#include "physics_load_tests.hpp"
#include <thread>

void physics_body_state_tests() {
    const double dt = double(1.f / 60), damping = 1. - .05 * dt;
    {
        Fixture f;
        f.physics->configure(PhysicsConfig{{0, 0, 0}});
        f.scene.restore_snapshot(free_body(2));
        const auto ref = cube_ref(f);
        check(!f.physics->body_state(ref) && !f.physics->point_velocity(ref, {}),
              "Unrealized body observation fabricated stationary state");
        f.physics->add_impulse(ref, {6, 0, 0});
        f.physics->add_angular_impulse(ref, {0, 0, 1});
        check(!f.physics->body_state(ref), "Read flushed pending initial impulses");
        f.tick();
        const auto state = f.physics->body_state(ref);
        check(state && state->motion == 2 && state->active &&
                  std::abs(state->linear_velocity[0] - 3 * damping) < 1e-6 &&
                  std::abs(state->angular_velocity[2] - 3 * damping) < 1e-6 &&
                  std::abs(state->centre_of_mass[0] - 3 * damping * dt) < 1e-6,
              "Body observation failed independent impulse/inertia/COM equation");
        const LocalTranslation point{state->centre_of_mass[0] + 1, state->centre_of_mass[1] + 2,
                                     state->centre_of_mass[2]};
        const auto velocity = f.physics->point_velocity(ref, point);
        check(velocity && std::abs((*velocity)[0] + 3 * damping) < 1e-5 &&
                  std::abs((*velocity)[1] - 3 * damping) < 1e-5 && (*velocity)[2] == 0,
              "World point velocity failed linear plus angular cross COM-offset equation");
        const auto central = f.physics->point_velocity(
            ref, {state->centre_of_mass[0], state->centre_of_mass[1], state->centre_of_mass[2]});
        check(central && *central == state->linear_velocity,
              "COM point velocity differs from copied native linear velocity");
        auto independent = *state;
        independent.linear_velocity[0] = 100;
        check(f.physics->body_state(ref) == state, "Mutating copied state changed the native body");
        f.physics->add_force(ref, {12, 0, 0});
        check(f.physics->body_state(ref) == state &&
                  f.physics->point_velocity(ref, point) == velocity,
              "Observation flushed a pending force or advanced simulation");
        f.tick();
        const auto next = f.physics->body_state(ref);
        check(next && std::abs(next->linear_velocity[0] - (3 * damping + 6 * dt) * damping) < 1e-6,
              "Copied velocity did not follow queued force at its next boundary");
        auto checkpoint = f.physics->checkpoint();
        Fixture recovered;
        recovered.physics->configure(PhysicsConfig{{0, 0, 0}});
        recovered.scene.restore_snapshot(f.scene.snapshot());
        recovered.physics->restore(checkpoint);
        check(recovered.physics->body_state(ref) == next,
              "Fresh recovery failed to report the actual continued solver state");
        for (double invalid : {1e9 + 1, std::numeric_limits<double>::infinity(),
                               std::numeric_limits<double>::quiet_NaN()})
            reject([&] { f.physics->point_velocity(ref, {invalid, 0, 0}); });
        check(f.physics->body_state(ref) == next, "Invalid observation point changed solver state");
        bool wrong_thread = false, wrong_point_thread = false;
        std::thread foreign([&] {
            try {
                (void)f.physics->body_state(ref);
            } catch (const std::exception&) {
                wrong_thread = true;
            }
            try {
                (void)f.physics->point_velocity(ref, {});
            } catch (const std::exception&) {
                wrong_point_thread = true;
            }
        });
        foreign.join();
        check(wrong_thread && wrong_point_thread,
              "Body/point observation bypassed owner-thread enforcement");
        auto entity = f.scene.entity("cube");
        auto settings = entity.get<PhysicsBody>();
        settings.mass = 4;
        settings.max_linear_velocity = 1;
        settings.max_angular_velocity = 60;
        entity.set<PhysicsBody>(settings);
        check(f.physics->body_state(ref) == next,
              "Read confused pending tuning with already realized replacement state");
        f.physics->synchronize(0);
        const auto replaced = f.physics->body_state(ref);
        check(replaced && replaced->active && replaced->centre_of_mass == next->centre_of_mass &&
                  replaced->linear_velocity == Double3{1, 0, 0} &&
                  std::abs(replaced->angular_velocity[2] - std::numbers::pi / 3) < 1e-6,
              "Body replacement observation lost pose or compatible clamped velocity");
        auto replacement_checkpoint = f.physics->checkpoint();
        Fixture replacement_recovered;
        replacement_recovered.physics->configure(PhysicsConfig{{0, 0, 0}});
        replacement_recovered.scene.restore_snapshot(f.scene.snapshot());
        replacement_recovered.physics->restore(replacement_checkpoint);
        check(replacement_recovered.physics->body_state(ref) == replaced,
              "Recovery observation lost actual replaced body velocity/COM/activity");
    }
    {
        Fixture f;
        free_world(f, 1);
        f.tick(360);
        const auto ref = cube_ref(f);
        const auto sleeping = f.physics->body_state(ref);
        check(sleeping && sleeping->motion == 2 && !sleeping->active,
              "Dynamic sleep observation precondition missing");
        const auto before = f.physics->checkpoint();
        for (unsigned read = 0; read < 10; ++read) {
            check(f.physics->body_state(ref) == sleeping &&
                      f.physics->point_velocity(ref, {1, 2, 3}) == Double3{},
                  "Reading sleeping body changed its velocity or activation");
        }
        check(f.physics->checkpoint() == before,
              "Read-only body observations changed private solver checkpoint bytes");
        auto entity = f.scene.entity("cube");
        auto settings = entity.get<PhysicsBody>();
        settings.enabled = false;
        entity.set<PhysicsBody>(settings);
        check(f.physics->body_state(ref) == sleeping,
              "Pending configuration was mistaken for already realized native state");
        f.physics->synchronize(0);
        check(!f.physics->body_state(ref) && !f.physics->point_velocity(ref, {}),
              "Disabled adopted body retained fabricated/stale observation");
        settings.enabled = true;
        entity.set<PhysicsBody>(settings);
        f.physics->synchronize(0);
        check(f.physics->body_state(ref).has_value(), "Re-enabled body state did not realize");
        const auto original = f.scene.snapshot();
        auto empty = original;
        empty["entities"] = Json::array();
        f.scene.restore_snapshot(empty);
        check(!f.physics->body_state(ref) && !f.physics->point_velocity(ref, {}),
              "Retired identity exposed still-cached native body state");
        f.physics->synchronize(0);
        f.scene.restore_snapshot(original);
        check(!f.physics->body_state(ref) && !f.physics->point_velocity(ref, {}),
              "Recreated same-UUID identity exposed an older native generation");
        f.physics->synchronize(0);
        check(f.physics->body_state(ref).has_value(),
              "Current same-UUID generation did not expose its newly realized body");
        f.physics->stop();
        reject([&] { f.physics->body_state(ref); });
        reject([&] { f.physics->point_velocity(ref, {}); });
    }
    {
        Fixture f;
        f.load(source());
        const auto ref = *f.engine.world().reference(f.scene.entity("floor").id());
        const auto state = f.physics->body_state(ref);
        check(state && state->motion == 0 && !state->active &&
                  state->linear_velocity == Double3{} && state->angular_velocity == Double3{} &&
                  f.physics->point_velocity(ref, {10, 20, 30}) == Double3{},
              "Valid Static body was confused with unavailable/sleeping Dynamic state");
    }
    {
        Fixture f;
        f.physics->configure(PhysicsConfig{{0, 0, 0}});
        f.load({{"version", 1}, {"entities", Json::array({body("platform", 1, 1)})}});
        const auto ref = *f.engine.world().reference(f.scene.entity("platform").id());
        f.physics->move_kinematic(ref, {0, 2, 0}, {});
        f.tick();
        const auto state = f.physics->body_state(ref);
        check(state && state->motion == 1 && std::abs(state->linear_velocity[1] - 1 / dt) < 1e-5 &&
                  std::abs(state->centre_of_mass[1] - 2) < 1e-6,
              "Kinematic observation did not use actual native target velocity");
    }
}
