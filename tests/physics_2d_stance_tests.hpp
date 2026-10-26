#pragma once
#include "physics_2d_mover_native_tests.hpp"

namespace forge::test {
inline void test_mover_stance_clearance() {
    using namespace physics_detail;
    for (double offset : {0., -999990., 999990.}) {
        MoverNativeFixture test;
        test.add(inline_configuration_2d({}, 0, {20, 1}, {1, 1, 1}), offset, offset - .5);
        test.add(inline_configuration_2d({}, 0, {2, .2f}, {1, 1, 1}), offset + 2, offset + 1);
        const auto actor = test.actor(offset, offset, .7f);
        const auto id = test.id(actor);
        test.synchronize();
        const auto before = test.native.mover_state(id, actor);
        std::size_t work = 0;
        check_mover(test.native.crouch_mover(id, actor, true, work), "Clear crouch was rejected");
        const auto crouched = test.native.mover_state(id, actor);
        check_mover(crouched->crouched && !crouched->shape_change_blocked &&
                        crouched->position == before->position && crouched->epoch == before->epoch,
                    "Crouching changed feet or realization identity");
        test.native.move_mover(id, actor, {3, -1});
        for (unsigned step = 0; step < 40; ++step)
            test.advance();
        const auto under = test.native.mover_state(id, actor);
        check_mover(std::abs(under->position[0] - offset - 2) < 1e-5 && under->support,
                    "Crouched physical motion did not pass under the low ceiling");
        work = 0;
        check_mover(!test.native.crouch_mover(id, actor, false, work),
                    "Standing grew through a low ceiling");
        const auto blocked = test.native.mover_state(id, actor);
        check_mover(blocked->crouched && blocked->shape_change_blocked &&
                        blocked->position == under->position && blocked->intent == under->intent &&
                        blocked->velocity == under->velocity,
                    "Blocked stance changed pose, intent, velocity or stance");
        check_mover(test.native.crouch_mover(id, actor, true, work) &&
                        !test.native.mover_state(id, actor)->shape_change_blocked,
                    "Requesting the current stance did not clear blocked status");
        for (unsigned step = 0; step < 60; ++step)
            test.advance();
        work = 0;
        const auto outside = test.native.mover_state(id, actor)->position;
        check_mover(test.native.crouch_mover(id, actor, false, work) &&
                        !test.native.mover_state(id, actor)->crouched &&
                        test.native.mover_state(id, actor)->position == outside,
                    "Standing outside the ceiling changed feet or stayed crouched");
        const Double2 ray_start{outside[0] - .7, outside[1] + 1.05};
        const Double2 ray_displacement{1.4, 0};
        const auto standing_hit = test.native.raycast(ray_start, ray_displacement, {});
        check_mover(standing_hit && standing_hit->entity == actor,
                    "Standing shape was not visible to ordinary geometric queries");
        check_mover(test.native.crouch_mover(id, actor, true, work) &&
                        !test.native.raycast(ray_start, ray_displacement, {}),
                    "Geometric query retained the old standing capsule after crouch");
    }
}
inline void test_mover_stance_filters_and_budget() {
    using namespace physics_detail;
    for (bool sensor : {false, true}) {
        MoverNativeFixture test;
        PhysicsBody2D obstacle;
        obstacle.sensor = sensor;
        obstacle.layer = 1;
        test.add(inline_configuration_2d(obstacle, 0, {2, .2f}, {1, 1, 1}), 0, 1);
        const auto actor = test.actor(0, 0);
        const auto id = test.id(actor);
        test.desired.at(id).config.body.mask = sensor ? UINT32_MAX : 1;
        test.synchronize();
        std::size_t work = 0;
        check_mover(test.native.crouch_mover(id, actor, true, work) &&
                        test.native.crouch_mover(id, actor, false, work),
                    "Stance clearance ignored the mutual physical mask");
    }
    MoverNativeFixture test;
    test.add(inline_configuration_2d({}, 0, {20, 1}, {1, 1, 1}), 0, -.5);
    const auto actor = test.actor(0, 0, .7f);
    test.synchronize();
    std::size_t work = 16384;
    bool rejected = false, faulted = false;
    try {
        test.native.crouch_mover(test.id(actor), actor, true, work);
    } catch (const std::exception&) {
        rejected = true;
    }
    try {
        (void)test.native.adopted_poses();
    } catch (const std::exception&) {
        faulted = true;
    }
    check_mover(rejected && faulted && work == 16384,
                "Stance work overflow permitted native adoption or exceeded its bound");
}
inline void test_mover_stance_candidates_and_recovery() {
    using namespace physics_detail;
    MoverNativeFixture original;
    const auto floor = original.add(inline_configuration_2d({}, 0, {20, 1}, {1, 1, 1}), 0, -.5);
    const auto actor = original.actor(0, 0, .7f);
    const auto id = original.id(actor);
    const auto synchronize = [&](const char* phase) {
        try {
            original.synchronize();
        } catch (const std::exception& error) {
            throw std::runtime_error(std::string(phase) + ": " + error.what());
        }
    };
    synchronize("Initial standing character");
    std::size_t work = 0;
    check_mover(original.native.crouch_mover(id, actor, true, work), "Initial crouch failed");
    const auto ceiling = original.add(inline_configuration_2d({}, 0, {4, .2f}, {1, 1, 1}), 0, 1.1);
    synchronize("New ceiling over crouched character");
    const auto previous_epoch = original.native.mover_state(id, actor)->epoch;
    original.desired.at(id).config.mover_profile->floor_probe = .2f;
    synchronize("Crouched configuration replacement");
    check_mover(original.native.mover_state(id, actor)->crouched &&
                    original.native.mover_state(id, actor)->epoch != previous_epoch,
                "Configuration replacement lost crouch state or geometry realization");
    original.advance();
    check_mover(!original.native.crouch_mover(id, actor, false, work),
                "Standing under the new ceiling was not blocked");
    original.native.move_mover(id, actor, {1, -1});
    const auto image = original.image();
    Desired2D desired{{7, original.desired.at(original.id(floor))},
                      {18, original.desired.at(original.id(ceiling))},
                      {92, original.desired.at(id)}};
    Native2D restored(original.config);
    restored.restore(image.metadata, image.bytes, desired, original.tick);
    const auto image_desired = desired;
    const auto initial = restored.mover_state(92, actor);
    check_mover(initial->crouched && initial->shape_change_blocked &&
                    initial->epoch == original.native.mover_state(id, actor)->epoch,
                "Fresh recovery lost effective crouch geometry or blocked status");
    for (unsigned step = 0; step < 20; ++step) {
        restored.expire_events();
        restored.step(1.f / 60, original.tick + 1);
        restored.adopt_events(original.tick + 1);
        const auto poses = restored.adopted_poses();
        for (const auto& [key, pose] : poses)
            desired.at(key).pose = pose;
        restored.accept_poses(poses);
        original.advance();
        const auto a = restored.mover_state(92, actor), b = original.native.mover_state(id, actor);
        check_mover(a->position == b->position && a->velocity == b->velocity &&
                        a->support == b->support && a->crouched == b->crouched &&
                        a->shape_change_blocked == b->shape_change_blocked,
                    "Crouched continuation diverged after rebound native recovery");
    }
    auto invalid = image.metadata;
    for (auto& mapping : invalid["mapping"])
        if (mapping.contains("mover"))
            mapping["mover"]["crouched"] = false; // Contradicts the native capsule snapshot.
    bool rejected = false;
    const auto retained = restored.mover_state(92, actor);
    Native2D invalid_target(original.config);
    try {
        invalid_target.restore(invalid, image.bytes, image_desired, original.tick - 20);
    } catch (const std::exception&) {
        rejected = true;
    }
    check_mover(rejected && !invalid_target.realized() &&
                    restored.mover_state(92, actor)->crouched &&
                    restored.mover_state(92, actor)->position == retained->position,
                "Inconsistent stance recovery replaced the admitted native world");
    MoverNativeFixture open;
    const auto free_actor = open.actor(0, 0);
    open.synchronize();
    work = 0;
    open.native.crouch_mover(open.id(free_actor), free_actor, true, work);
    open.advance();
    auto inconsistent = open.image();
    inconsistent.metadata["mapping"][0]["mover"]["crouched"] = false;
    Native2D fresh(open.config);
    std::string diagnostic;
    try {
        fresh.restore(inconsistent.metadata, inconsistent.bytes, open.desired, open.tick);
    } catch (const std::exception& error) {
        diagnostic = error.what();
    }
    check_mover(diagnostic.find("geometry/mass mismatch") != std::string::npos && !fresh.realized(),
                "Native snapshot stance mismatch escaped geometry validation in open space");
    auto invalid_standing = open.desired;
    auto& configuration = invalid_standing.at(open.id(free_actor)).config;
    configuration.dimensions[1] = 1001;
    configuration.mover_profile->crouch_height = .3f;
    auto oversized = open.image();
    oversized.metadata["mapping"][0]["configuration"] = configuration.json();
    Native2D invalid_source(open.config);
    diagnostic.clear();
    try {
        invalid_source.restore(oversized.metadata, oversized.bytes, invalid_standing, open.tick);
    } catch (const std::exception& error) {
        diagnostic = error.what();
    }
    check_mover(diagnostic.find("dimensions exceed") != std::string::npos &&
                    !invalid_source.realized(),
                "Valid crouch geometry concealed invalid authored standing dimensions");
}
inline void test_mover_stance_sensor_events() {
    using namespace physics_detail;
    MoverNativeFixture test;
    PhysicsBody2D sensor;
    sensor.sensor = true;
    const auto volume = test.add(inline_configuration_2d(sensor, 0, {1, .1f}, {1, 1, 1}), 0, 1.1);
    const auto actor = test.actor(0, 0);
    test.synchronize();
    test.advance();
    const auto has = [&](bool begin) {
        for (std::size_t index = 0; index < test.native.event_count(); ++index) {
            const auto event = test.native.event(index);
            if (event->sensor && event->first == volume && event->second == actor &&
                event->begin == begin)
                return true;
        }
        return false;
    };
    check_mover(has(true), "Standing controller did not enter the head-height sensor");
    std::size_t work = 0;
    test.native.crouch_mover(test.id(actor), actor, true, work);
    test.advance();
    check_mover(has(false), "Shrinking the actual capsule did not emit the sensor End");
    test.native.crouch_mover(test.id(actor), actor, false, work);
    test.advance();
    check_mover(has(true), "Standing again did not emit a fresh sensor Begin");
}
inline void test_mover_stance_one_way_clearance() {
    using namespace physics_detail;
    MoverNativeFixture test;
    PhysicsBody2D platform;
    platform.one_way = true;
    test.add(inline_configuration_2d(platform, 0, {4, .2f}, {1, 1, 1}), 0, 1.5);
    const auto actor = test.actor(0, 0, .7f);
    const auto id = test.id(actor);
    test.synchronize();
    test.native.move_mover(id, actor, {0, 6});
    for (unsigned step = 0; step < 6; ++step)
        test.advance();
    const auto before = test.native.mover_state(id, actor);
    std::size_t work = 0;
    check_mover(!test.native.crouch_mover(id, actor, true, work),
                "Crouch clearance bypassed an overlapping one-way shape");
    const auto blocked = test.native.mover_state(id, actor);
    check_mover(!blocked->crouched && blocked->shape_change_blocked &&
                    blocked->position == before->position && blocked->intent == before->intent &&
                    blocked->velocity == before->velocity,
                "Blocked shrink changed the current passing capsule or motion");
    for (unsigned step = 0; step < 14; ++step)
        test.advance();
    work = 0;
    check_mover(test.native.crouch_mover(id, actor, true, work) &&
                    test.native.mover_state(id, actor)->crouched &&
                    !test.native.mover_state(id, actor)->shape_change_blocked,
                "Clear stance retry remained blocked after leaving the one-way surface");
}
} // namespace forge::test
