#pragma once
#include "physics_2d_mover_support_tests.hpp"
#include <iostream>

namespace forge::test {
inline void test_mover_steps() {
    using namespace physics_detail;
    for (double offset : {0., -999990., 999990.}) {
        for (double direction : {-1., 1.}) {
            for (bool enabled : {false, true}) {
                MoverNativeFixture test;
                test.add(inline_configuration_2d({}, 0, {16, .4f}, {1, 1, 1}), offset, offset - .2);
                for (unsigned i = 0; i < 3; ++i) {
                    const float top = .25f * (i + 1);
                    test.add(inline_configuration_2d({}, 0, {1, top}, {1, 1, 1}),
                             offset + direction * i, offset + .5 * top);
                }
                const auto actor = test.actor(offset - direction * 2, offset, .7f);
                const auto id = test.id(actor);
                test.desired.at(id).config.mover_profile->step_height = enabled ? .3f : 0;
                test.synchronize();
                test.native.move_mover(id, actor, {direction * 2, -.2});
                double highest = offset;
                for (unsigned tick = 0; tick < 135; ++tick) {
                    const auto before = test.native.mover_state(id, actor)->position;
                    test.advance();
                    const auto state = test.native.mover_state(id, actor);
                    highest = std::max(highest, state->position[1]);
                    check_mover((state->position[0] - before[0]) * direction <=
                                    2. * float(1.f / 60) + .001,
                                "Step probe added horizontal travel beyond the request");
                    if (state->position[1] - before[1] > .1)
                        check_mover(state->velocity[1] <= .001,
                                    "Step correction became upward jump velocity");
                }
                const auto state = test.native.mover_state(id, actor);
                if (enabled)
                    check_mover((state->position[0] - offset) * direction > 2.45 &&
                                    highest > offset + .73 && state->support &&
                                    state->support->walkable,
                                "Enabled controller did not traverse the three ordinary stairs");
                else
                    check_mover((state->position[0] - offset) * direction < -.5 &&
                                    highest < offset + .02,
                                "Default-off controller gained automatic stair climbing");
            }
        }
    }
}
inline void test_mover_step_rejections() {
    using namespace physics_detail;
    for (unsigned fixture = 0; fixture < 5; ++fixture) {
        std::array<Double2, 2> endpoints{};
        for (unsigned enabled = 0; enabled < 2; ++enabled) {
            MoverNativeFixture test;
            if (fixture != 2)
                test.add(inline_configuration_2d({}, 0, {16, .4f}, {1, 1, 1}), 0, -.2);
            const float top = fixture == 0 ? .8f : .25f;
            test.add(inline_configuration_2d({}, 0, {4, top}, {1, 1, 1}), 2, .5 * top);
            if (fixture == 1)
                test.add(inline_configuration_2d({}, 0, {4, .2f}, {1, 1, 1}), 0, 1.35);
            const auto actor = test.actor(-1, 0, fixture == 3 ? -1 : .7f);
            const auto id = test.id(actor);
            test.desired.at(id).config.mover_profile->step_height = enabled ? .3f : 0;
            test.synchronize();
            test.native.move_mover(id, actor, {2, fixture == 4 ? .01 : -.2});
            for (unsigned tick = 0; tick < 70; ++tick)
                test.advance();
            endpoints[enabled] = test.native.mover_state(id, actor)->position;
            if (fixture < 2)
                check_mover(endpoints[enabled][0] < 0 && endpoints[enabled][1] < .06,
                            "Step climbed tall or low-headroom geometry");
        }
        // Floating and explicit upward intent can already project onto capsule
        // corners. They must retain that ordinary behavior, not be forced to stop.
        check_mover(endpoints[0] == endpoints[1],
                    "Step candidate changed rejected or excluded ordinary motion");
    }
}
inline void test_mover_step_effective_geometry_and_filters() {
    using namespace physics_detail;
    for (unsigned fixture = 0; fixture < 4; ++fixture) {
        MoverNativeFixture test;
        test.add(inline_configuration_2d({}, 0, {16, .4f}, {1, 1, 1}), 0, -.2);
        PhysicsBody2D obstacle;
        obstacle.sensor = fixture == 1;
        obstacle.mask = fixture == 2 ? 0 : UINT32_MAX;
        obstacle.one_way = fixture == 3;
        test.add(inline_configuration_2d(obstacle, 0, {4, .25f}, {1, 1, 1}), 2, .125);
        if (fixture == 0)
            test.add(inline_configuration_2d({}, 0, {4, .2f}, {1, 1, 1}), 0, 1.35);
        const auto actor = test.actor(-1, 0, .7f);
        const auto id = test.id(actor);
        test.desired.at(id).config.mover_profile->step_height = .3f;
        test.synchronize();
        if (fixture == 0) {
            std::size_t work = 0;
            check_mover(test.native.crouch_mover(id, actor, true, work),
                        "Step fixture could not crouch");
        }
        test.native.move_mover(id, actor, {2, -.2});
        for (unsigned tick = 0; tick < 70; ++tick)
            test.advance();
        const auto state = test.native.mover_state(id, actor);
        check_mover(state->position[0] > 1.3 && state->support && state->support->walkable,
                    "Crouched step or filtered-obstacle traversal was blocked");
        if (fixture == 0)
            check_mover(state->crouched && std::abs(state->position[1] - .25) < .01,
                        "Step ignored effective crouched headroom or its tread");
        else
            check_mover(std::abs(state->position[1]) < .01,
                        "Step climbed a sensor, masked obstacle or passable one-way side");
    }
}
inline void test_mover_step_recovery() {
    using namespace physics_detail;
    MoverNativeFixture original;
    const auto floor = original.add(inline_configuration_2d({}, 0, {16, .4f}, {1, 1, 1}), 0, -.2);
    const auto stair = original.add(inline_configuration_2d({}, 0, {4, .25f}, {1, 1, 1}), 2, .125);
    const auto actor = original.actor(-1, 0, .7f);
    const auto id = original.id(actor);
    original.desired.at(id).config.mover_profile->step_height = .3f;
    original.synchronize();
    original.native.move_mover(id, actor, {2, -.2});
    bool stepped = false;
    for (unsigned tick = 0; tick < 30; ++tick) {
        original.advance();
        const auto state = original.native.mover_state(id, actor);
        if (state->position[1] > .15) {
            stepped = true;
            check_mover(!state->support, "Recovery fixture missed the unsupported leading edge");
            break;
        }
    }
    check_mover(stepped, "Recovery fixture did not step");
    const auto image = original.image();
    Desired2D rebound{{18, original.desired.at(original.id(floor))},
                      {7, original.desired.at(original.id(stair))},
                      {92, original.desired.at(id)}};
    Native2D restored(original.config);
    restored.restore(image.metadata, image.bytes, rebound, original.tick);
    auto mismatched = rebound;
    mismatched.at(92).config.mover_profile->step_height = 0;
    bool rejected = false;
    try {
        Native2D invalid(original.config);
        invalid.restore(image.metadata, image.bytes, mismatched, original.tick);
    } catch (const std::exception&) {
        rejected = true;
    }
    check_mover(rejected, "Recovery accepted changed step-height configuration");
    for (unsigned tick = 0; tick < 40; ++tick) {
        original.advance();
        restored.expire_events();
        restored.step(1.f / 60, original.tick);
        const auto poses = restored.adopted_poses();
        restored.adopt_events(original.tick);
        for (const auto& [native, pose] : poses)
            rebound.at(native).pose = pose;
        restored.accept_poses(poses);
        const auto expected = original.native.mover_state(id, actor);
        const auto observed = restored.mover_state(92, actor);
        check_mover(expected->position == observed->position &&
                        expected->velocity == observed->velocity &&
                        expected->support == observed->support,
                    "Leading-edge stair continuation diverged after fresh rebound recovery");
    }
}

inline void test_mover_step_carry_and_work_bound() {
    using namespace physics_detail;
    MoverNativeFixture ride;
    PhysicsBody2D kinematic;
    kinematic.motion = 1;
    const auto platform =
        ride.add(inline_configuration_2d(kinematic, 0, {16, .4f}, {1, 1, 1}), 0, -.2);
    ride.add(inline_configuration_2d({}, 0, {4, .25f}, {1, 1, 1}), 2, .125);
    const auto actor = ride.actor(-1, 0, .7f);
    const auto id = ride.id(actor), platform_id = ride.id(platform);
    auto& profile = *ride.desired.at(id).config.mover_profile;
    profile.platform_carry = true;
    profile.step_height = .3f;
    ride.synchronize();
    ride.native.move_mover(id, actor, {2, -.2});
    for (unsigned tick = 0; tick < 100; ++tick) {
        ride.desired.at(platform_id).pose.translation.x += .01;
        ride.advance();
    }
    const auto state = ride.native.mover_state(id, actor);
    check_mover(state->position[0] > 2 && std::abs(state->position[1] - .25) < .01 &&
                    state->support && state->support->walkable,
                "Moving-platform transport prevented the following stair solve");
    // Same832-part world fits ordinary movement but exhausts the shared budget
    // when the optional stair queries are added; native state must fault closed.
    constexpr unsigned count = 832;
    for (bool enabled : {false, true}) {
        MoverNativeFixture test;
        Body2DConfiguration floor;
        floor.shape = 3;
        floor.parts =
            std::make_shared<const std::vector<BoxPart2D>>(count, BoxPart2D{{0, -.2f}, {16, .4f}});
        test.add(floor, 0, 0);
        test.add(inline_configuration_2d({}, 0, {4, .25f}, {1, 1, 1}), 2, .125);
        const auto actor = test.actor(-.3, 0, .7f);
        const auto id = test.id(actor);
        test.desired.at(id).config.mover_profile->step_height = enabled ? .3f : 0;
        test.synchronize();
        test.native.move_mover(id, actor, {2, -.2});
        std::string error;
        try {
            test.advance();
        } catch (const std::exception& failure) {
            error = failure.what();
        }
        if (!enabled) {
            check_mover(error.empty(), "Ordinary budget comparison unexpectedly faulted");
            continue;
        }
        check_mover(error.find("work capacity 16384") != std::string::npos,
                    "Step queries bypassed the shared fixed-work cap");
        bool faulted = false;
        try {
            (void)test.native.adopted_poses();
        } catch (const std::exception&) {
            faulted = true;
        }
        check_mover(faulted, "Step query overflow allowed partial native pose adoption");
    }
    std::cout << "Step shared-work differential PASS:832 floor parts\n";
}

} // namespace forge::test
