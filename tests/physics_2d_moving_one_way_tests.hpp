#pragma once
#include "physics_2d_one_way_tests.hpp"
#include <iostream>

namespace forge::test::moving_one_way_detail {
using namespace physics_detail;
struct Rig {
    MoverNativeFixture test;
    EntityRef floor, actor;
    Double2 transport;
    Rig(bool moving, bool painted, float sign, double origin, double degrees, Double2 velocity,
        unsigned scenario, bool one_way = true)
        : test([&] {
              auto c = MoverGeometryFixture::settings();
              if (scenario < 2 || scenario >= 4) {
                  c.gravity = {0, -9.81 * sign, 0};
              }
              return c;
          }()),
          transport(moving ? velocity : Double2{}) {
        const double angle = degrees * std::numbers::pi / 180;
        const LocalRotation rotation{0, 0, float(std::sin(angle / 2)), float(std::cos(angle / 2))};
        const Double2 normal{-std::sin(angle) * sign, std::cos(angle) * sign};
        PhysicsBody2D body;
        body.motion = moving ? 1 : 0;
        body.one_way = one_way;
        body.friction = 0;
        floor = test.add(one_way_floor(body, painted, sign), origin - (painted ? 7 * normal[0] : 0),
                         origin - (painted ? 7 * normal[1] : 0), rotation);
        test.desired.at(test.id(floor)).pose.scale.y = sign;
        body = {};
        body.motion = 2;
        body.friction = 0;
        body.lock_rotation = true;
        body.allow_sleeping = false;
        body.bullet = scenario >= 2;
        const double start = scenario == 4 ? .2 : scenario == 5 ? .345 : scenario % 2 == 0 ? -2 : 2;
        actor = test.add(inline_configuration_2d(body, 1, {.25f, 0}, {1, 1, 1}),
                         origin + start * normal[0], origin + start * normal[1]);
        test.synchronize();
        const double speed = scenario >= 4   ? 0
                             : scenario == 0 ? 8
                             : scenario == 1 ? -2
                             : scenario == 2 ? 80
                                             : -80;
        one_way_velocity(test, actor,
                         {speed * normal[0] + transport[0], speed * normal[1] + transport[1]});
    }
    void advance() {
        auto& target = test.desired.at(test.id(floor)).pose.translation;
        target.x += transport[0] * double(1.f / 60);
        target.y += transport[1] * double(1.f / 60);
        test.advance();
    }
};
inline void native_control() {
    double position_error = 0, velocity_error = 0;
    for (bool painted : {false, true})
        for (float sign : {1.f, -1.f})
            for (double origin : {0., -999800., 999800.})
                for (double degrees : {0., -20., 20.})
                    for (Double2 velocity : {Double2{2, 1}, Double2{-2, -1}})
                        for (unsigned scenario : {1u, 3u, 5u}) {
                            Rig reference(false, painted, sign, origin, degrees, velocity, scenario,
                                          false);
                            Rig moving(true, painted, sign, origin, degrees, velocity, scenario,
                                       false);
                            Rig one_way(true, painted, sign, origin, degrees, velocity, scenario,
                                        true);
                            for (unsigned frame = 0; frame < 120; ++frame) {
                                reference.advance();
                                moving.advance();
                                one_way.advance();
                                const auto a = reference.test.native.state(
                                    reference.test.id(reference.actor), reference.actor);
                                const auto p = reference.test.native.state(
                                    reference.test.id(reference.floor), reference.floor);
                                const auto b = moving.test.native.state(
                                    moving.test.id(moving.actor), moving.actor);
                                const auto q = moving.test.native.state(
                                    moving.test.id(moving.floor), moving.floor);
                                const auto c = one_way.test.native.state(
                                    one_way.test.id(one_way.actor), one_way.actor);
                                for (unsigned axis = 0; axis < 2; ++axis) {
                                    position_error =
                                        std::max(position_error,
                                                 std::abs((a->position[axis] - p->position[axis]) -
                                                          (b->position[axis] - q->position[axis])));
                                    velocity_error = std::max(velocity_error,
                                                              std::abs(a->linear_velocity[axis] -
                                                                       (b->linear_velocity[axis] -
                                                                        q->linear_velocity[axis])));
                                    if (std::abs(c->position[axis] - b->position[axis]) > .00001 ||
                                        std::abs(c->linear_velocity[axis] -
                                                 b->linear_velocity[axis]) > .00001)
                                        throw std::runtime_error(
                                            "Moving front-response differs from identical ordinary "
                                            "native platform");
                                }
                            }
                        }
    // Native contact correction differs between Static and Kinematic targets.
    // The same moving one-way/ordinary response above must still match to1e-5.
    check_mover(position_error <= .03 && velocity_error <= .0001,
                "Ordinary native reference exceeded the measured contact envelope");
    std::cout << "Ordinary native Static/Kinematic relative differences: position="
              << position_error << " velocity=" << velocity_error << '\n';
}
inline void galilean() {
    unsigned cases = 0;
    for (bool painted : {false, true})
        for (float sign : {1.f, -1.f})
            for (double origin : {0., -999800., 999800.})
                for (double degrees : {0., -20., 20.})
                    for (Double2 velocity : {Double2{2, 1}, Double2{-2, -1}})
                        for (unsigned scenario = 0; scenario < 6; ++scenario) {
                            Rig reference(false, painted, sign, origin, degrees, velocity,
                                          scenario);
                            Rig moving(true, painted, sign, origin, degrees, velocity, scenario);
                            for (unsigned frame = 0; frame < 90; ++frame) {
                                reference.advance();
                                moving.advance();
                                const auto a = reference.test.native.state(
                                    reference.test.id(reference.actor), reference.actor);
                                const auto p = reference.test.native.state(
                                    reference.test.id(reference.floor), reference.floor);
                                const auto b = moving.test.native.state(
                                    moving.test.id(moving.actor), moving.actor);
                                const auto q = moving.test.native.state(
                                    moving.test.id(moving.floor), moving.floor);
                                for (unsigned axis = 0; axis < 2; ++axis)
                                    if (std::abs((a->position[axis] - p->position[axis]) -
                                                 (b->position[axis] - q->position[axis])) > .03 ||
                                        std::abs(a->linear_velocity[axis] -
                                                 (b->linear_velocity[axis] -
                                                  q->linear_velocity[axis])) > .004) {
                                        std::ostringstream s;
                                        s << "Moving one-way translation invariance failed: "
                                             "painted="
                                          << painted << " sign=" << sign << " origin=" << origin
                                          << " degrees=" << degrees << " scenario=" << scenario
                                          << " frame=" << frame << " axis=" << axis
                                          << " relative=" << (b->position[axis] - q->position[axis])
                                          << " reference="
                                          << (a->position[axis] - p->position[axis]);
                                        throw std::runtime_error(s.str());
                                    }
                            }
                            ++cases;
                        }
    std::cout << cases
              << " native Galilean Box/offset-Map/sign/rotation/origin/pass/CCD "
                 "cases PASS\n";
}
inline void rotating_rigid() {
    using namespace forge;
    unsigned cases = 0;
    for (bool painted : {false, true})
        for (float sign : {1.f, -1.f})
            for (float spin : {-1.f, 1.f})
                for (unsigned scenario : {1u, 2u, 3u, 5u}) {
                    Rig ordinary(true, painted, sign, 0, 0, {1, -.5}, scenario, false);
                    Rig one_way(true, painted, sign, 0, 0, {1, -.5}, scenario);
                    for (unsigned step = 1; step <= (scenario == 2 ? 5u : 90u); ++step) {
                        for (auto* rig : {&ordinary, &one_way}) {
                            rig->test.desired.at(rig->test.id(rig->floor)).pose.rotation =
                                rotation_about_axis({0, 0, 1}, spin * 20.f * step / 90.f);
                            rig->advance();
                        }
                        const auto a = ordinary.test.native.state(ordinary.test.id(ordinary.actor),
                                                                  ordinary.actor);
                        const auto b = one_way.test.native.state(one_way.test.id(one_way.actor),
                                                                 one_way.actor);
                        for (unsigned axis = 0; axis < 2; ++axis) {
                            if (scenario == 2) {
                                const double initial = axis ? -2 * sign : 0;
                                const double speed = axis ? 80 * sign - .5 : 1;
                                check_mover(std::abs(b->position[axis] -
                                                     (initial + speed * step * double(1.f / 60))) <
                                                1e-5,
                                            "Rotating back-face passage differs from independent "
                                            "free motion");
                            } else {
                                check_mover(std::abs(a->position[axis] - b->position[axis]) <
                                                    1e-5 &&
                                                std::abs(a->linear_velocity[axis] -
                                                         b->linear_velocity[axis]) < 1e-5,
                                            "Rotating one-way front differs from identical "
                                            "ordinary native platform");
                            }
                        }
                    }
                    ++cases;
                }
    std::cout << cases << " actually rotating native rigid Box/Map/mirror/CCD cases PASS\n";
}
inline void carry() {
    unsigned cases = 0;
    for (bool painted : {false, true})
        for (double origin : {0., -999800., 999800.})
            for (Double2 delta : {Double2{.03, .01}, Double2{-.03, -.01}}) {
                MoverNativeFixture test;
                PhysicsBody2D platform;
                platform.motion = 1;
                platform.one_way = true;
                const auto ground =
                    test.add(one_way_floor(platform, painted), origin, origin - (painted ? 7 : 0));
                const auto actor = test.actor(origin, origin + .1, .7f);
                test.desired.at(test.id(actor)).config.mover_profile->platform_carry = true;
                test.synchronize();
                test.native.move_mover(test.id(actor), actor, {0, -1});
                const auto initial = test.native.state(test.id(ground), ground)->position;
                std::unique_ptr<MoverNativeFixture> recovered;
                for (unsigned step = 0; step < 50; ++step) {
                    auto& target = test.desired.at(test.id(ground)).pose.translation;
                    target.x += delta[0];
                    target.y += delta[1];
                    test.advance();
                    const auto state = test.native.mover_state(test.id(actor), actor);
                    if (recovered) {
                        auto& copy_target =
                            recovered->desired.at(recovered->id(ground)).pose.translation;
                        copy_target.x += delta[0];
                        copy_target.y += delta[1];
                        recovered->advance();
                        const auto copy =
                            recovered->native.mover_state(recovered->id(actor), actor);
                        check_mover(
                            copy->position == state->position &&
                                copy->velocity == state->velocity &&
                                bool(copy->support) == bool(state->support),
                            "Moving one-way character recovery changed transport or support");
                    }
                    const auto floor = test.native.state(test.id(ground), ground);
                    if (!state->support || !state->support->walkable ||
                        state->support->entity != ground ||
                        std::abs(state->position[0] - origin - (floor->position[0] - initial[0])) >
                            .001 ||
                        std::abs(state->position[1] - origin - .1 -
                                 (floor->position[1] - initial[1])) > .006)
                        throw std::runtime_error(
                            "Moving one-way character support/transport failed");
                    if (step == 24) {
                        const auto image = test.image();
                        recovered = std::make_unique<MoverNativeFixture>(test.config);
                        recovered->desired = test.desired;
                        recovered->tick = test.tick;
                        recovered->native.restore(image.metadata, image.bytes, recovered->desired,
                                                  recovered->tick);
                    }
                }
                ++cases;
            }
    std::cout << cases << " translating Box/Map Character2D support/transport cases PASS\n";
}
inline void rotation_and_recovery() {
    for (bool painted : {false, true}) {
        MoverNativeFixture test;
        PhysicsBody2D platform;
        platform.motion = 1;
        platform.one_way = true;
        const auto ground = test.add(one_way_floor(platform, painted), 0, painted ? -7 : 0);
        const auto actor = test.actor(2, .1, .7f);
        test.desired.at(test.id(actor)).config.mover_profile->platform_carry = true;
        test.synchronize();
        for (unsigned step = 1; step <= 20; ++step) {
            test.desired.at(test.id(ground)).pose.rotation =
                rotation_about_axis({0, 0, 1}, float(step));
            test.advance();
            const auto state = test.native.mover_state(test.id(actor), actor);
            const auto floor = test.native.state(test.id(ground), ground);
            const double c = std::cos(floor->angle), s = std::sin(floor->angle),
                         y = painted ? 7.4 : .4;
            check_mover(
                state->support && state->support->walkable && state->support->entity == ground &&
                    std::abs(state->position[0] - (floor->position[0] + 2 * c - y * s)) < .001 &&
                    std::abs(state->position[1] - (floor->position[1] + 2 * s + y * c - .3)) < .006,
                "Rotating one-way character support/rigid transport failed");
        }
        for (unsigned phase : {0u, 30u, 80u}) {
            Rig original(true, painted, 1, 999900, 0, {1, -1}, 0);
            for (unsigned step = 0; step < phase; ++step)
                original.advance();
            const auto image = original.test.image();
            MoverNativeFixture copy(original.test.config);
            copy.desired = original.test.desired;
            copy.tick = original.test.tick;
            copy.native.restore(image.metadata, image.bytes, copy.desired, copy.tick);
            for (unsigned step = 0; step < 120; ++step) {
                original.advance();
                auto& target = copy.desired.at(copy.id(original.floor)).pose.translation;
                target.x += original.transport[0] * double(1.f / 60);
                target.y += original.transport[1] * double(1.f / 60);
                copy.advance();
                for (auto entity : {original.floor, original.actor}) {
                    const auto a = original.test.native.state(original.test.id(entity), entity);
                    const auto b = copy.native.state(copy.id(entity), entity);
                    check_mover(
                        a->position == b->position && a->linear_velocity == b->linear_velocity &&
                            a->angle == b->angle,
                        "Moving one-way fresh native recovery changed body pose or velocity");
                }
            }
        }
    }
    std::cout
        << "Rotating Box/Map character transport and six exact moving-world recoveries PASS\n";
}
} // namespace forge::test::moving_one_way_detail

namespace forge::test {
inline void test_moving_one_way_native() {
    moving_one_way_detail::native_control();
    moving_one_way_detail::galilean();
    moving_one_way_detail::rotating_rigid();
    moving_one_way_detail::carry();
    moving_one_way_detail::rotation_and_recovery();
}
} // namespace forge::test
