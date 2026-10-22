#pragma once
#include "physics_2d_mover_native_tests.hpp"

namespace forge::test {
inline double mover_slope_feet(double x, double degrees) {
    const double angle = degrees * std::numbers::pi / 180;
    return (.8 + std::sin(angle) * x) / std::cos(angle) - .8;
}
inline void test_mover_support_slopes() {
    using namespace physics_detail;
    const float floor_y = float(std::cos(50 * std::numbers::pi / 180));
    for (double offset : {0., -999990., 999990.}) {
        for (double slope : {0., 30., 75.}) {
            MoverNativeFixture test;
            const auto floor = test.add(inline_configuration_2d({}, 0, {20, 1}, {1, 1, 1}), offset,
                                        offset - .5, rotation_about_axis({0, 0, 1}, float(slope)));
            const auto actor = test.actor(offset, offset + mover_slope_feet(0, slope), floor_y);
            const auto id = test.id(actor);
            test.synchronize();
            const auto initial = test.native.mover_state(id, actor);
            check_mover(
                initial && initial->support && initial->support->entity == floor &&
                    initial->support->epoch == test.native.state(test.id(floor), floor)->epoch &&
                    initial->support->walkable == (slope < 50) &&
                    std::abs(initial->support->normal[1] -
                             std::cos(slope * std::numbers::pi / 180)) < 1e-5 &&
                    initial->support->velocity == Double2{},
                "Initial support lost slope classification, canonical reference, epoch or normal");
            const double plane =
                -std::sin(slope * std::numbers::pi / 180) * (initial->support->point[0] - offset) +
                std::cos(slope * std::numbers::pi / 180) *
                    (initial->support->point[1] - offset + .5);
            if (std::abs(plane - .5) >= 1e-4)
                throw std::runtime_error("Support point was not on the actual rotated floor: " +
                                         Json{{"offset", offset},
                                              {"slope", slope},
                                              {"point", initial->support->point},
                                              {"normal", initial->support->normal},
                                              {"plane", plane}}
                                             .dump());
            if (slope != 30)
                continue;
            test.native.move_mover(id, actor, {-3, 0});
            for (unsigned step = 0; step < 20; ++step) {
                test.advance();
                const auto state = test.native.mover_state(id, actor);
                const double x = state->position[0] - offset;
                check_mover(
                    state->support && state->support->walkable &&
                        std::abs(x + (step + 1) * 3. * float(1.f / 60)) < 1e-5 &&
                        std::abs(state->position[1] - offset - mover_slope_feet(x, slope)) < .008,
                    "Descending floor follow lost support, changed requested X travel or hovered");
            }
            test.native.move_mover(id, actor, {0, 3});
            check_mover(!test.native.mover_state(id, actor)->support,
                        "Upward intent retained grounded floor-follow state");
            const auto before_jump = test.native.mover_state(id, actor)->position;
            test.advance();
            check_mover(!test.native.mover_state(id, actor)->support &&
                            test.native.mover_state(id, actor)->position[1] > before_jump[1] + .049,
                        "Floor following pulled explicit upward motion back to the slope");
        }
        for (bool floating : {false, true}) {
            MoverNativeFixture test;
            test.add(inline_configuration_2d({}, 0, {20, 1}, {1, 1, 1}), offset, offset - .5,
                     rotation_about_axis({0, 0, 1}, 30));
            const auto actor =
                test.actor(offset, offset + mover_slope_feet(0, 30), floating ? -1 : floor_y);
            const auto id = test.id(actor);
            test.desired.at(id).config.mover_profile->floor_probe = 0;
            test.synchronize();
            const auto initial = test.native.mover_state(id, actor)->position;
            test.native.move_mover(id, actor, {-3, 0});
            test.advance();
            const auto state = test.native.mover_state(id, actor);
            check_mover(!state->support && std::abs(state->position[1] - initial[1]) < 1e-5,
                        "Floating mode or disabled floor probe applied hidden downward motion");
        }
    }
}
inline void test_mover_support_velocity_and_lifetime() {
    using namespace physics_detail;
    MoverNativeFixture test;
    PhysicsBody2D platform;
    platform.motion = 1;
    const auto floor = test.add(inline_configuration_2d(platform, 0, {20, 1}, {1, 1, 1}), 0, -.5);
    const auto actor = test.actor(0, 0, .7f);
    const auto id = test.id(actor), floor_id = test.id(floor);
    test.synchronize();
    test.native.move_mover(id, actor, {0, -1});
    test.desired.at(floor_id).pose.translation.x = .1;
    test.advance();
    const auto state = test.native.mover_state(id, actor);
    check_mover(state->support && state->support->entity == floor && state->support->walkable &&
                    std::abs(state->support->velocity[0] - 6) < 1e-4 &&
                    std::abs(state->support->velocity[1]) < 1e-5 &&
                    std::abs(state->position[0]) < 1e-5,
                "Kinematic support velocity was lost or automatic horizontal carry was introduced");
    const auto previous_epoch = state->support->epoch;
    test.desired.at(floor_id).config.body.friction = .8f;
    test.synchronize();
    check_mover(test.native.mover_state(id, actor)->support &&
                    test.native.mover_state(id, actor)->support->epoch != previous_epoch &&
                    !test.native.current(floor_id, floor, previous_epoch),
                "Support observation retained a replaced body's private epoch");
    test.desired.erase(floor_id);
    test.synchronize();
    check_mover(!test.native.mover_state(id, actor)->support,
                "Removed support remained published in controller state");
}
inline void test_mover_support_curves() {
    using namespace physics_detail;
    for (double offset : {0., -999990., 999990.})
        for (unsigned kind : {1u, 2u}) {
            MoverNativeFixture test;
            const auto floor =
                test.add(inline_configuration_2d({}, kind, {2, kind == 1 ? 0.f : 4.f}, {1, 1, 1}),
                         offset, offset - 2, rotation_about_axis({0, 0, 1}, 90));
            const auto actor = test.actor(offset, offset, .7f);
            test.synchronize();
            const auto state = test.native.mover_state(test.id(actor), actor);
            check_mover(state->support && state->support->entity == floor &&
                            state->support->normal[1] > .9999 &&
                            std::abs(state->support->point[0] - offset) < 1e-4 &&
                            std::abs(state->support->point[1] - offset) < 1e-4,
                        "Curved support returned its core point instead of the physical surface");
        }
    for (bool lower_first : {false, true}) {
        MoverNativeFixture test;
        test.add(inline_configuration_2d({}, 0, {20, 1}, {1, 1, 1}), 0, -.5);
        auto lower = test.actor(0, 0, .7f), upper = test.actor(0, 1.2, .7f);
        const auto lower_id = test.id(lower), upper_id = test.id(upper);
        lower.entity = EntityId::parse(lower_first ? "00000000-0000-4000-8000-000000000001"
                                                   : "00000000-0000-4000-8000-000000000002");
        upper.entity = EntityId::parse(lower_first ? "00000000-0000-4000-8000-000000000002"
                                                   : "00000000-0000-4000-8000-000000000001");
        test.desired.at(lower_id).ref = lower;
        test.desired.at(upper_id).ref = upper;
        test.synchronize();
        test.native.move_mover(test.id(lower), lower, {1, 0});
        test.native.move_mover(test.id(upper), upper, {0, -1});
        test.advance();
        const auto a = test.native.mover_state(test.id(lower), lower);
        const auto b = test.native.mover_state(test.id(upper), upper);
        check_mover(
            a && b && b->support && b->support->entity == lower && b->support->epoch == a->epoch &&
                b->support->velocity == a->velocity && b->support->point[1] > 1.19,
            "Controller support lost final-phase velocity, epoch or capsule surface position");
    }
    MoverNativeFixture spinning;
    PhysicsBody2D kinematic;
    kinematic.motion = 1;
    const auto disk = spinning.add(inline_configuration_2d(kinematic, 1, {2, 0}, {1, 1, 1}), 0, -2);
    const auto standing = spinning.actor(0, 0, .7f);
    spinning.synchronize();
    spinning.desired.at(spinning.id(disk)).pose.rotation =
        rotation_about_axis({0, 0, 1}, float(.1 * 180 / std::numbers::pi));
    spinning.advance();
    const auto supported = spinning.native.mover_state(spinning.id(standing), standing);
    check_mover(
        supported->support && std::abs(supported->support->point[1]) < 1e-5 &&
            std::abs(supported->support->velocity[0] + 12) < .001 &&
            std::abs(supported->support->velocity[1]) < 1e-5,
        "Angular support velocity used the circle core instead of omega cross surface radius");
}
inline void test_mover_support_recovery() {
    using namespace physics_detail;
    MoverNativeFixture original;
    const auto floor = original.add(inline_configuration_2d({}, 0, {20, 1}, {1, 1, 1}), 0, -.5,
                                    rotation_about_axis({0, 0, 1}, 30));
    const auto actor = original.actor(0, mover_slope_feet(0, 30), .7f);
    original.synchronize();
    original.native.move_mover(original.id(actor), actor, {-3, 0});
    original.advance();
    const auto image = original.image();
    Desired2D rebound;
    rebound.emplace(108, original.desired.at(original.id(actor)));
    rebound.emplace(7, original.desired.at(original.id(floor)));
    Native2D restored(original.config);
    restored.restore(image.metadata, image.bytes, rebound, original.tick);
    check_mover(restored.mover_state(108, actor)->support ==
                    original.native.mover_state(original.id(actor), actor)->support,
                "Fresh recovery did not derive the same copied canonical ground observation");
    for (unsigned step = 0; step < 10; ++step) {
        restored.expire_events();
        restored.step(1.f / 60, original.tick + 1);
        const auto poses = restored.adopted_poses();
        restored.adopt_events(original.tick + 1);
        for (const auto& [id, pose] : poses)
            rebound.at(id).pose = pose;
        restored.accept_poses(poses);
        original.advance();
        const auto a = restored.mover_state(108, actor),
                   b = original.native.mover_state(original.id(actor), actor);
        check_mover(a && b && a->position == b->position && a->velocity == b->velocity &&
                        a->support == b->support,
                    "Floor-follow continuation diverged after fresh native recovery");
    }
}
} // namespace forge::test
