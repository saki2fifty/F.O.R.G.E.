#pragma once
#include "physics_2d_mover_support_tests.hpp"

namespace forge::test {
inline void test_mover_floor_stop() {
    using namespace physics_detail;
    for (double offset : {0., -999990., 999990.}) {
        for (double slope : {-30., 30., 75.}) {
            for (bool floating : {false, true}) {
                std::array<Double2, 2> endpoints{};
                for (unsigned stop = 0; stop < 2; ++stop) {
                    MoverNativeFixture test;
                    test.add(inline_configuration_2d({}, 0, {20, 1}, {1, 1, 1}), offset,
                             offset - .5, rotation_about_axis({0, 0, 1}, float(slope)));
                    const auto actor = test.actor(offset, offset + mover_slope_feet(0, slope) + .2,
                                                  floating ? -1 : .7f);
                    const auto id = test.id(actor);
                    test.desired.at(id).config.mover_profile->floor_stop_on_slope = stop;
                    test.synchronize();
                    const auto start = test.native.mover_state(id, actor)->position;
                    test.native.move_mover(id, actor, {0, -3});
                    for (unsigned tick = 0; tick < 25; ++tick)
                        test.advance();
                    const auto state = test.native.mover_state(id, actor);
                    endpoints[stop] = state->position;
                    if (stop && !floating && std::abs(slope) < 50) {
                        check_mover(std::abs(state->position[0] - offset) < .002 &&
                                        state->position[1] < start[1] - .15 && state->support &&
                                        state->support->walkable &&
                                        std::hypot(state->velocity[0], state->velocity[1]) < 1e-5 &&
                                        state->intent == Double2{0, -3},
                                    "Slope stopping lost checked approach, support or idle intent");
                        const auto resting = state->position;
                        test.advance();
                        check_mover(test.native.mover_state(id, actor)->position == resting,
                                    "Touching slope stopping drifted on a repeated request");
                        test.native.move_mover(id, actor, {1, 0});
                        test.advance();
                        check_mover(test.native.mover_state(id, actor)->position[0] >
                                        resting[0] + .01,
                                    "Slope stopping suppressed explicit horizontal movement");
                        test.native.move_mover(id, actor, {0, 3});
                        const auto before_jump = test.native.mover_state(id, actor)->position;
                        test.advance();
                        check_mover(test.native.mover_state(id, actor)->position[1] >
                                        before_jump[1] + .04,
                                    "Slope stopping suppressed an explicit jump");
                    }
                }
                if (!floating && std::abs(slope) < 50)
                    check_mover(std::abs(endpoints[0][0] - offset) > .3,
                                "Legacy slope response no longer projects downward intent");
                else
                    check_mover(endpoints[0] == endpoints[1],
                                "Slope stopping changed floating or steep-wall motion");
            }
        }
    }
}
inline void test_mover_ceiling_response() {
    using namespace physics_detail;
    for (double offset : {0., -999990., 999990.}) {
        for (double angle : {-30., 0., 30.}) {
            for (bool slide : {false, true}) {
                MoverNativeFixture test;
                test.add(inline_configuration_2d({}, 0, {20, 1}, {1, 1, 1}), offset, offset + 2.5,
                         rotation_about_axis({0, 0, 1}, float(angle)));
                const auto actor = test.actor(offset, offset, .7f);
                const auto id = test.id(actor);
                test.desired.at(id).config.mover_profile->slide_on_ceiling = slide;
                test.synchronize();
                test.native.move_mover(id, actor, {3, 8});
                bool collided = false;
                for (unsigned tick = 0; tick < 15; ++tick) {
                    test.advance();
                    const auto state = test.native.mover_state(id, actor);
                    if (state->velocity != Double2{3, 8}) {
                        collided = true;
                        if (!slide) {
                            check_mover(
                                state->velocity == Double2{} && state->position[0] > offset + .05 &&
                                    state->position[1] > offset + .2,
                                "Ceiling stop lost approach travel or retained tangent velocity");
                            const auto resting = state->position;
                            test.advance();
                            const auto again = test.native.mover_state(id, actor);
                            check_mover(std::hypot(again->position[0] - resting[0],
                                                   again->position[1] - resting[1]) < .006 &&
                                            again->velocity == Double2{},
                                        "Already touching ceiling ignored the stop policy");
                            test.native.move_mover(id, actor, {0, -3});
                            test.advance();
                            const auto falling = test.native.mover_state(id, actor);
                            check_mover(
                                std::abs(falling->position[0] - again->position[0]) < 1e-5 &&
                                    falling->position[1] < again->position[1] - .04,
                                "Ceiling stop prevented later game-supplied vertical falling");
                        } else
                            check_mover(std::hypot(state->velocity[0], state->velocity[1]) > 1,
                                        "Default ceiling response lost sliding velocity");
                        break;
                    }
                }
                check_mover(collided, "Ceiling response fixture did not encounter its ceiling");
            }
        }
    }
}
inline void test_mover_surface_response_exclusions() {
    using namespace physics_detail;
    for (unsigned fixture = 0; fixture < 5; ++fixture) {
        std::array<Double2, 2> endpoints{};
        for (unsigned enabled = 0; enabled < 2; ++enabled) {
            MoverNativeFixture test;
            PhysicsBody2D obstacle;
            obstacle.sensor = fixture == 0;
            obstacle.mask = fixture == 1 ? 0 : UINT32_MAX;
            obstacle.one_way = fixture == 2;
            const float angle = fixture == 4 ? 75.f : 30.f;
            test.add(inline_configuration_2d(obstacle, 0, {20, 1}, {1, 1, 1}), 0,
                     fixture == 4 ? 4.5 : 2.5, rotation_about_axis({0, 0, 1}, angle));
            const auto actor = test.actor(0, 0, fixture == 3 ? -1 : .7f);
            const auto id = test.id(actor);
            auto& profile = *test.desired.at(id).config.mover_profile;
            profile.floor_stop_on_slope = enabled;
            profile.slide_on_ceiling = !enabled;
            test.synchronize();
            test.native.move_mover(id, actor, {fixture == 4 ? -3. : 3., 8});
            for (unsigned tick = 0; tick < 15; ++tick)
                test.advance();
            endpoints[enabled] = test.native.mover_state(id, actor)->position;
        }
        check_mover(endpoints[0] == endpoints[1], "Surface policy changed sensors, masks, passable "
                                                  "one-way, floating or steep wall response");
    }
    MoverNativeFixture ride;
    PhysicsBody2D kinematic;
    kinematic.motion = 1;
    const auto platform =
        ride.add(inline_configuration_2d(kinematic, 0, {4, .4f}, {1, 1, 1}), 0, -.2);
    const auto actor = ride.actor(0, 0, .7f);
    const auto id = ride.id(actor), support_id = ride.id(platform);
    auto& profile = *ride.desired.at(id).config.mover_profile;
    profile.platform_carry = true;
    profile.floor_stop_on_slope = true;
    profile.slide_on_ceiling = false;
    ride.synchronize();
    ride.native.move_mover(id, actor, {0, -1});
    for (unsigned tick = 0; tick < 10; ++tick) {
        ride.desired.at(support_id).pose.translation.x += .05;
        ride.desired.at(support_id).pose.translation.y += .02;
        ride.advance();
    }
    const auto state = ride.native.mover_state(id, actor);
    check_mover(std::abs(state->position[0] - .5) < .001 &&
                    std::abs(state->position[1] - .2) < .006 && state->support &&
                    state->velocity == Double2{},
                "Idle surface stopping suppressed separate platform transport");
}
inline void test_mover_surface_response_recovery() {
    using namespace physics_detail;
    MoverNativeFixture original;
    const auto floor = original.add(inline_configuration_2d({}, 0, {20, 1}, {1, 1, 1}), 0, -.5,
                                    rotation_about_axis({0, 0, 1}, 30));
    const auto actor = original.actor(0, mover_slope_feet(0, 30), .7f);
    const auto id = original.id(actor);
    auto& profile = *original.desired.at(id).config.mover_profile;
    profile.floor_stop_on_slope = true;
    profile.slide_on_ceiling = false;
    original.synchronize();
    original.native.move_mover(id, actor, {0, -3});
    original.advance();
    const auto image = original.image();
    Desired2D rebound{{7, original.desired.at(original.id(floor))}, {92, original.desired.at(id)}};
    Native2D restored(original.config);
    restored.restore(image.metadata, image.bytes, rebound, original.tick);
    original.advance();
    restored.expire_events();
    restored.step(1.f / 60, original.tick);
    const auto expected = original.native.mover_state(id, actor);
    const auto observed = restored.mover_state(92, actor);
    check_mover(expected->position == observed->position &&
                    expected->velocity == observed->velocity &&
                    expected->support == observed->support,
                "Surface response diverged after fresh rebound recovery");
    rebound.at(92).config.mover_profile->floor_stop_on_slope = false;
    bool rejected = false;
    try {
        Native2D invalid(original.config);
        invalid.restore(image.metadata, image.bytes, rebound, original.tick - 1);
    } catch (const std::exception&) {
        rejected = true;
    }
    check_mover(rejected, "Recovery accepted changed surface-response configuration");
}
} // namespace forge::test
