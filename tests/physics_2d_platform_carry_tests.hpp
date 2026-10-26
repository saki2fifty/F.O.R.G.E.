#pragma once
#include "physics_2d_mover_native_tests.hpp"

namespace forge::test {
inline void test_platform_carry_motion() {
    using namespace physics_detail;
    for (double offset : {0., -999990., 999990.})
        for (const Double2 displacement : {Double2{.1, 0}, Double2{0, .1}, Double2{0, -.1},
                                           Double2{.1, .05}, Double2{-.1, -.05}}) {
            MoverNativeFixture test;
            PhysicsBody2D kinematic;
            kinematic.motion = 1;
            const auto floor = test.add(inline_configuration_2d(kinematic, 0, {20, 1}, {1, 1, 1}),
                                        offset, offset - .5);
            const auto actor = test.actor(offset, offset, .7f);
            const auto id = test.id(actor), floor_id = test.id(floor);
            test.desired.at(id).config.mover_profile->platform_carry = true;
            test.synchronize();
            test.native.move_mover(id, actor, {1, -1});
            for (unsigned step = 0; step < 5; ++step) {
                test.desired.at(floor_id).pose.translation.x += displacement[0];
                test.desired.at(floor_id).pose.translation.y += displacement[1];
                test.advance();
                const auto state = test.native.mover_state(id, actor);
                const auto platform = test.native.state(floor_id, floor);
                check_mover(state && state->support && state->support->entity == floor &&
                                state->support->walkable &&
                                std::abs(state->position[0] - platform->position[0] -
                                         (step + 1) * double(1.f / 60)) < 1e-5 &&
                                std::abs(state->position[1] - platform->position[1] - .5) < .006 &&
                                std::abs(state->velocity[0] - 1) < 1e-5 &&
                                std::abs(state->velocity[1]) < 1e-5,
                            "Transport lost rigid displacement, relative intent or final support");
            }
        }
    for (bool carry : {false, true})
        for (bool floating : {false, true})
            for (bool jumping : {false, true}) {
                MoverNativeFixture test;
                PhysicsBody2D kinematic;
                kinematic.motion = 1;
                const auto floor =
                    test.add(inline_configuration_2d(kinematic, 0, {20, 1}, {1, 1, 1}), 0, -.5);
                const auto actor = test.actor(0, 0, floating ? -1 : .7f);
                const auto id = test.id(actor);
                test.desired.at(id).config.mover_profile->platform_carry = carry;
                test.synchronize();
                test.native.move_mover(id, actor, {0, jumping ? 3. : -1.});
                test.desired.at(test.id(floor)).pose.translation.x = .1;
                test.advance();
                const auto state = test.native.mover_state(id, actor);
                check_mover(
                    std::abs(state->position[0] - (carry && !floating && !jumping ? .1 : 0)) < 1e-5,
                    "Default-off, Floating or explicit jump acquired platform transport");
            }
}
inline void test_platform_carry_rotation() {
    using namespace physics_detail;
    for (double offset : {0., -999990., 999990.}) {
        MoverNativeFixture test;
        PhysicsBody2D kinematic;
        kinematic.motion = 1;
        const auto floor = test.add(inline_configuration_2d(kinematic, 0, {20, 1}, {1, 1, 1}),
                                    offset, offset - .5);
        const auto actor = test.actor(offset + 2, offset, .7f);
        const auto id = test.id(actor), floor_id = test.id(floor);
        test.desired.at(id).config.mover_profile->platform_carry = true;
        test.synchronize();
        test.desired.at(floor_id).pose.rotation = rotation_about_axis({0, 0, 1}, 2);
        test.advance();
        const auto state = test.native.mover_state(id, actor);
        const auto platform = test.native.state(floor_id, floor);
        const double c = std::cos(platform->angle), s = std::sin(platform->angle);
        check_mover(state->support && state->support->entity == floor &&
                        std::abs(state->position[0] - offset - (2 * c - .8 * s)) < .001 &&
                        std::abs(state->position[1] - offset - (2 * s + .8 * c - .8)) < .006 &&
                        state->velocity == Double2{} &&
                        test.native.admitted(id)->pose.rotation == LocalRotation{},
                    "Angular platform transport lost its offset or rotated the upright capsule");
        test.native.move_mover(id, actor, {0, -1});
        for (unsigned degrees = 3; degrees <= 30; ++degrees) {
            test.desired.at(floor_id).pose.rotation =
                rotation_about_axis({0, 0, 1}, float(degrees));
            test.advance();
            const auto carried = test.native.mover_state(id, actor);
            check_mover(carried->support && carried->support->walkable &&
                            carried->support->entity == floor && carried->velocity[1] <= 0,
                        "Gradual platform rotation lost walkable support or added jump velocity");
        }
    }
}
inline void test_platform_carry_obstruction() {
    using namespace physics_detail;
    for (bool crush : {false, true}) {
        MoverNativeFixture test;
        PhysicsBody2D kinematic;
        kinematic.motion = 1;
        const auto floor =
            test.add(inline_configuration_2d(kinematic, 0, {20, 1}, {1, 1, 1}), 0, -.5);
        const auto actor = test.actor(0, 0, .7f);
        const auto id = test.id(actor);
        test.desired.at(id).config.mover_profile->platform_carry = true;
        test.add(inline_configuration_2d(
                     {}, 0, crush ? std::array<float, 2>{20, 1} : std::array<float, 2>{.2f, 4},
                     {1, 1, 1}),
                 crush ? 0 : .8, crush ? 1.75 : 1);
        test.synchronize();
        if (crush)
            test.desired.at(test.id(floor)).pose.translation.y += .2;
        else
            test.desired.at(test.id(floor)).pose.translation.x += 2;
        if (!crush) {
            test.advance();
            const auto state = test.native.mover_state(id, actor);
            check_mover(state->position[0] > .39 && state->position[0] < .41 && state->support &&
                            state->velocity == Double2{},
                        "Carried character crossed a wall or reported carry as locomotion");
            continue;
        }
        bool failed = false, faulted = false;
        try {
            test.advance();
        } catch (const std::exception&) {
            failed = true;
        }
        try {
            (void)test.native.adopted_poses();
        } catch (const std::exception&) {
            faulted = true;
        }
        check_mover(failed && faulted, "Crushed character admitted an embedded partial step");
    }
}
inline void test_platform_carry_lifetime_recovery() {
    using namespace physics_detail;
    MoverNativeFixture original;
    PhysicsBody2D kinematic;
    kinematic.motion = 1;
    const auto floor =
        original.add(inline_configuration_2d(kinematic, 0, {20, 1}, {1, 1, 1}), 0, -.5);
    const auto actor = original.actor(0, 0, .7f);
    const auto id = original.id(actor), floor_id = original.id(floor);
    original.desired.at(id).config.mover_profile->platform_carry = true;
    original.synchronize();
    original.native.move_mover(id, actor, {1, -1});
    original.desired.at(floor_id).pose.translation.x += .1;
    original.advance();
    const auto image = original.image();
    Desired2D desired{{17, original.desired.at(floor_id)}, {88, original.desired.at(id)}};
    Native2D restored(original.config);
    restored.restore(image.metadata, image.bytes, desired, original.tick);
    for (unsigned step = 0; step < 5; ++step) {
        desired.at(17).pose.translation.x += .1;
        desired.at(17).pose.translation.y += .05;
        original.desired.at(floor_id).pose = desired.at(17).pose;
        restored.expire_events();
        std::vector<std::uint64_t> snaps;
        restored.synchronize(desired, desired, 1.f / 60, {}, snaps);
        restored.finish_targets(1.f / 60);
        restored.step(1.f / 60, original.tick + 1);
        restored.adopt_events(original.tick + 1);
        const auto poses = restored.adopted_poses();
        for (const auto& [key, pose] : poses)
            desired.at(key).pose = pose;
        restored.accept_poses(poses);
        original.advance();
        const auto a = restored.mover_state(88, actor), b = original.native.mover_state(id, actor);
        check_mover(a->position == b->position && a->velocity == b->velocity &&
                        a->support == b->support,
                    "Fresh native recovery changed platform transport continuation");
    }
    // A discontinuous static source move must not become a platform delta.
    original.desired.at(floor_id).config.body.motion = 0;
    original.synchronize();
    const auto before = original.native.mover_state(id, actor)->position;
    original.native.move_mover(id, actor, {});
    original.desired.at(floor_id).pose.translation.x += 1;
    original.advance();
    check_mover(std::abs(original.native.mover_state(id, actor)->position[0] - before[0]) < 1e-5,
                "Static relocation retained transport history across a body replacement");
    original.desired.erase(floor_id);
    original.advance();
    check_mover(!original.native.mover_state(id, actor)->support,
                "Removed carrier retained a dangling support binding");
}
inline void test_platform_carry_dynamic_and_teleport() {
    using namespace physics_detail;
    MoverNativeFixture test;
    PhysicsBody2D dynamic;
    dynamic.motion = 2;
    dynamic.friction = 0;
    dynamic.linear_damping = 0;
    const auto floor = test.add(inline_configuration_2d(dynamic, 0, {20, 1}, {1, 1, 1}), 0, -.5);
    const auto actor = test.actor(0, 0, .7f);
    const auto id = test.id(actor), floor_id = test.id(floor);
    test.desired.at(id).config.mover_profile->platform_carry = true;
    test.synchronize();
    BodyCommand command{};
    command.ref = floor;
    command.planar = true;
    command.kind = BodyCommandKind::LinearVelocity;
    command.value = {6, 0, 0};
    std::vector<std::uint64_t> snaps;
    test.native.apply(floor_id, command, snaps);
    test.advance();
    const auto state = test.native.mover_state(id, actor);
    check_mover(state->support && state->position[0] > .09 &&
                    std::abs(state->position[0] - test.native.state(floor_id, floor)->position[0]) <
                        1e-5 &&
                    state->velocity == Double2{},
                "Dynamic support transport used intent instead of actual rigid displacement");
    command.kind = BodyCommandKind::Teleport;
    command.p = {3, -.5, 7};
    command.clear = true;
    test.native.apply(floor_id, command, snaps);
    const auto placed = test.native.state(floor_id, floor);
    test.desired.at(floor_id).pose.translation = {placed->position[0], placed->position[1], 7};
    test.native.validate(test.desired, 1.f / 60, {floor_id});
    test.native.synchronize(test.desired, test.desired, 1.f / 60, {floor_id}, snaps);
    const auto previous = state->position;
    test.advance();
    const auto after = test.native.mover_state(id, actor)->position;
    check_mover(std::abs(after[0] - previous[0]) < 1e-5 && std::abs(after[1] - previous[1]) < .006,
                "Explicit support teleport was replayed as continuous platform transport");
    // A real shape edit still uses detached admission, even on a supported body.
    auto candidate = test.desired;
    candidate.at(floor_id).config.dimensions[1] = 4;
    bool rejected = false;
    try {
        test.native.validate(candidate, 1.f / 60);
    } catch (const std::exception&) {
        rejected = true;
    }
    check_mover(rejected, "Carry weakened edited-obstacle admission");
    test.native.check();
}
} // namespace forge::test
