#pragma once
#include "physics_2d_mover_native_tests.hpp"
#include <sstream>

namespace forge::test {
inline physics_detail::Body2DConfiguration one_way_floor(PhysicsBody2D body, bool painted,
                                                         float sign = 1) {
    using namespace physics_detail;
    if (!painted)
        return inline_configuration_2d(body, 0, {10, .2f}, {1, sign, 1});
    Body2DConfiguration config;
    config.body = body;
    config.shape = 3;
    // Offset and separated roofs expose the old body-origin/inline-height assumption.
    config.parts = std::make_shared<const std::vector<BoxPart2D>>(std::vector<BoxPart2D>{
        {{0, 7 * sign}, {10, .2f}}, {{20, 12 * sign}, {2, .2f}}, {{-20, -4 * sign}, {2, .2f}}});
    return config;
}
inline void test_one_way_profile() {
    using namespace physics_detail;
    auto rejected = [](auto operation) {
        try {
            operation();
        } catch (const std::exception&) {
            return;
        }
        throw std::runtime_error("Unsupported one-way profile admitted");
    };
    PhysicsBody2D platform;
    platform.one_way = true;
    for (unsigned motion : {1u, 2u}) {
        platform.motion = motion;
        rejected([&] { inline_configuration_2d(platform, 0, {4, .2f}, {1, 1, 1}); });
    }
    platform.motion = 0;
    platform.sensor = true;
    rejected([&] { inline_configuration_2d(platform, 0, {4, .2f}, {1, 1, 1}); });
    platform.sensor = false;
    for (unsigned shape : {1u, 2u})
        rejected([&] { inline_configuration_2d(platform, shape, {.3f, .6f}, {1, 1, 1}); });
    for (float margin : {0.f, .02f, 1.f}) {
        platform.one_way_margin = margin;
        const auto config = inline_configuration_2d(platform, 0, {4, .2f}, {-1, -1, 0});
        check_mover(config.body.one_way && config.body.one_way_margin == margin,
                    "One-way profile lost supported margin/signed scale");
    }
}
inline void one_way_velocity(MoverNativeFixture& test, EntityRef ref, Double2 value,
                             float angular = 0) {
    using namespace physics_detail;
    BodyCommand command{};
    command.ref = ref;
    command.planar = true;
    command.kind = BodyCommandKind::LinearVelocity;
    command.value = {value[0], value[1], 0};
    std::vector<std::uint64_t> snaps;
    test.native.apply(test.id(ref), command, snaps);
    if (angular != 0) {
        command.kind = BodyCommandKind::AngularVelocity;
        command.value = {0, 0, angular};
        test.native.apply(test.id(ref), command, snaps);
    }
}
inline void test_one_way_rigid(bool painted = false) {
    using namespace physics_detail;
    unsigned observations = 0;
    for (const double origin : {0., -999900., 999900.})
        for (const double degrees : {0., -30., 30.})
            for (const float sign : {1.f, -1.f})
                for (unsigned shape = 0; shape != 3; ++shape)
                    for (unsigned scenario = 0; scenario != 6; ++scenario) {
                        const double radians = degrees * std::numbers::pi / 180;
                        const Double2 normal{-std::sin(radians) * sign, std::cos(radians) * sign};
                        const LocalRotation rotation{0, 0, float(std::sin(radians / 2)),
                                                     float(std::cos(radians / 2))};
                        auto settings = MoverGeometryFixture::settings();
                        const bool gravity = scenario < 4;
                        if (gravity)
                            settings.gravity = {-9.81 * normal[0], -9.81 * normal[1], 0};
                        MoverNativeFixture test{settings};
                        PhysicsBody2D platform;
                        platform.one_way = true;
                        const auto ground =
                            test.add(one_way_floor(platform, painted, sign),
                                     origin - (painted ? 7 * normal[0] : 0),
                                     origin - (painted ? 7 * normal[1] : 0), rotation);
                        test.desired.at(test.id(ground)).pose.scale.y = sign;
                        PhysicsBody2D body;
                        body.motion = 2;
                        body.lock_rotation = true;
                        body.allow_sleeping = false;
                        body.bullet = scenario >= 4;
                        const std::array<float, 2> dimensions = shape == 0   ? std::array{.4f, .5f}
                                                                : shape == 1 ? std::array{.25f, 0.f}
                                                                             : std::array{.2f, .3f};
                        const double radius = shape == 2 ? .35 : .25;
                        const double start = scenario == 2                    ? radius + .1 - .15
                                             : scenario == 3                  ? radius + .1 - .005
                                             : scenario == 0 || scenario == 4 ? -2
                                                                              : 2;
                        const auto actor = test.add(
                            inline_configuration_2d(body, shape, dimensions, {1, 1, 1}),
                            origin + start * normal[0], origin + start * normal[1], rotation);
                        test.synchronize();
                        const double speed = scenario == 0   ? 8
                                             : scenario == 1 ? -2
                                             : scenario == 4 ? 80
                                             : scenario == 5 ? -80
                                                             : 0;
                        one_way_velocity(test, actor, {speed * normal[0], speed * normal[1]});
                        double maximum = start;
                        const unsigned steps = scenario == 0 ? 240 : 60;
                        for (unsigned step = 0; step < steps; ++step) {
                            test.advance();
                            const auto state = test.native.state(test.id(actor), actor);
                            const double distance = (state->position[0] - origin) * normal[0] +
                                                    (state->position[1] - origin) * normal[1];
                            maximum = std::max(maximum, distance);
                        }
                        const auto state = test.native.state(test.id(actor), actor);
                        const double distance = (state->position[0] - origin) * normal[0] +
                                                (state->position[1] - origin) * normal[1];
                        const double speed_after = state->linear_velocity[0] * normal[0] +
                                                   state->linear_velocity[1] * normal[1];
                        bool passed;
                        if (scenario == 2)
                            passed = distance < -4;
                        else if (scenario == 4)
                            passed =
                                std::abs(distance - 78) < .002 && std::abs(speed_after - 80) < .002;
                        else
                            passed = std::abs(distance - radius - .1) < .025 &&
                                     std::abs(speed_after) < .02 && (scenario != 0 || maximum > 1);
                        if (!passed) {
                            std::ostringstream message;
                            message << "One-way integrated rigid case failed: origin=" << origin
                                    << " painted=" << painted << " degrees=" << degrees
                                    << " sign=" << sign << " shape=" << shape
                                    << " scenario=" << scenario << " distance=" << distance
                                    << " speed=" << speed_after << " maximum=" << maximum;
                            throw std::runtime_error(message.str());
                        }
                        ++observations;
                    }
    check_mover(observations == 324, "One-way matrix did not execute every rigid case");
}
inline void test_one_way_contact_stability() {
    using namespace physics_detail;
    for (bool painted : {false, true}) {
        auto settings = MoverGeometryFixture::settings();
        settings.gravity = {0, -9.81, 0};
        MoverNativeFixture test{settings};
        PhysicsBody2D platform;
        platform.one_way = true;
        Body2DConfiguration floor;
        if (painted) {
            floor.body = platform;
            floor.shape = 3;
            floor.parts = std::make_shared<const std::vector<BoxPart2D>>(
                std::vector<BoxPart2D>{{{0, -3.5f}, {10, 1}}, {{20, 4}, {2, 1}}});
        } else
            floor = inline_configuration_2d(platform, 0, {10, 1}, {1, 1, 1});
        test.add(floor, 0, painted ? 0 : -3.5);
        PhysicsBody2D body;
        body.motion = 2;
        body.lock_rotation = true;
        body.restitution = 0;
        const auto actor =
            test.add(inline_configuration_2d(body, 0, {.9f, .9f}, {1, 1, 1}), -3.5, 4);
        test.synchronize();
        std::unique_ptr<MoverNativeFixture> recovered;
        for (unsigned step = 0; step < 240; ++step) {
            test.advance();
            if (step == 72) {
                const auto image = test.image();
                recovered = std::make_unique<MoverNativeFixture>(settings);
                recovered->desired = test.desired;
                recovered->tick = test.tick;
                recovered->native.restore(image.metadata, image.bytes, recovered->desired,
                                          recovered->tick);
            } else if (recovered) {
                recovered->advance();
                const auto a = test.native.state(test.id(actor), actor);
                const auto b = recovered->native.state(recovered->id(actor), actor);
                check_mover(a->position == b->position && a->linear_velocity == b->linear_velocity,
                            "Recovered soft one-way contact lost native continuation evidence");
            }
        }
        const auto state = test.native.state(test.id(actor), actor);
        if (!state || std::abs(state->position[1] + 2.55) > .025)
            throw std::runtime_error(
                "Long fall lost one-way contact: painted=" + std::to_string(painted) +
                ", y=" + std::to_string(state ? state->position[1] : 0));
        one_way_velocity(test, actor, {0, 4});
        double maximum = -2.55;
        for (unsigned step = 0; step < 90; ++step) {
            test.advance();
            maximum = std::max(maximum, test.native.state(test.id(actor), actor)->position[1]);
        }
        check_mover(maximum > -2 && std::abs(test.native.state(test.id(actor), actor)->position[1] +
                                             2.55) < .025,
                    "Qualified one-way contact prevented a rigid jump or subsequent landing");
        one_way_velocity(test, actor, {0, -.01}); // Wake before the discontinuity.
        BodyCommand teleport{};
        teleport.ref = actor;
        teleport.planar = true;
        teleport.kind = BodyCommandKind::Teleport;
        teleport.p = {-3.5, -2.7, 0};
        std::vector<std::uint64_t> snaps;
        test.native.apply(test.id(actor), teleport, snaps);
        for (unsigned step = 0; step < 120; ++step)
            test.advance();
        check_mover(test.native.state(test.id(actor), actor)->position[1] < -4,
                    "Discontinuous deep placement inherited an old one-way contact");
    }
}
inline void test_one_way_point_motion() {
    using namespace physics_detail;
    for (double origin : {0., -999900., 999900.})
        for (double degrees : {0., -30., 30.})
            for (bool approaching_point : {false, true}) {
                const double plane = degrees * std::numbers::pi / 180;
                const double angle = plane + std::numbers::pi / 6;
                const Double2 normal{-std::sin(plane), std::cos(plane)};
                MoverNativeFixture test;
                PhysicsBody2D platform;
                platform.one_way = true;
                test.add(inline_configuration_2d(platform, 0, {10, .2f}, {1, 1, 1}), origin, origin,
                         {0, 0, float(std::sin(plane / 2)), float(std::cos(plane / 2))});
                PhysicsBody2D body;
                body.motion = 2;
                body.allow_sleeping = false;
                const double center = .1 + .5 + .05 * std::cos(std::numbers::pi / 6);
                const auto actor =
                    test.add(inline_configuration_2d(body, 0, {2, .1f}, {1, 1, 1}),
                             origin + normal[0] * center, origin + normal[1] * center,
                             {0, 0, float(std::sin(angle / 2)), float(std::cos(angle / 2))});
                test.synchronize();
                const double speed = approaching_point ? .2 : -.2;
                const float angular = approaching_point ? 2 : -2;
                one_way_velocity(test, actor, {speed * normal[0], speed * normal[1]}, angular);
                test.advance();
                const auto state = test.native.state(test.id(actor), actor);
                const double after =
                    state->linear_velocity[0] * normal[0] + state->linear_velocity[1] * normal[1];
                const bool passed = approaching_point
                                        ? after > .21 && state->angular_velocity < 1.99
                                        : std::abs(after + .2) < .0001 &&
                                              std::abs(state->angular_velocity + 2) < .0001;
                if (!passed) {
                    std::ostringstream message;
                    message << "Angular contact-point admission failed: origin=" << origin
                            << " degrees=" << degrees << " approaching_point=" << approaching_point
                            << " center_speed=" << after << " angular=" << state->angular_velocity;
                    throw std::runtime_error(message.str());
                }
            }
}
inline void test_one_way_character(bool painted = false) {
    using namespace physics_detail;
    for (const double origin : {0., -999900., 999900.}) {
        MoverNativeFixture test;
        PhysicsBody2D platform;
        platform.one_way = true;
        const auto ground =
            test.add(one_way_floor(platform, painted), origin, origin + 2 - (painted ? 7 : 0));
        const auto actor = test.actor(origin, origin, .7f);
        test.synchronize();
        const auto id = test.id(actor);
        // Both public geometric casts still observe the underside, including
        // when the ignored canonical ref names this real controller.
        PhysicsSweep2D query;
        query.shape = PhysicsOverlap2D::Shape::Capsule;
        query.dimensions = {.3, .6};
        query.origin = {origin, origin + .6};
        query.displacement = {0, 3};
        query.ignore = actor;
        const auto before = test.image();
        const auto geometric = test.native.shape_cast(query, {});
        const auto touching = test.native.mover_cast(query, {});
        check_mover(geometric && touching && geometric->entity == ground &&
                        touching->entity == ground,
                    "One-way response leaked into public geometric queries");
        check_mover_unchanged(test, before);
        double vertical = 8, maximum = origin;
        for (unsigned step = 0; step != 240; ++step) {
            vertical -= 9.81 * float(1.f / 60);
            test.native.move_mover(id, actor, {0, vertical});
            test.advance();
            const auto state = test.native.mover_state(id, actor);
            maximum = std::max(maximum, state->position[1]);
            if (state->support && state->support->walkable && vertical <= 0)
                vertical = 0;
        }
        const auto state = test.native.mover_state(id, actor);
        check_mover(maximum > origin + 3 && std::abs(state->position[1] - origin - 2.1) < .015 &&
                        state->support && state->support->entity == ground &&
                        state->support->walkable && std::abs(state->velocity[1]) < 1e-5,
                    "Native character did not jump through and land on the one-way face");
        std::size_t work = 0;
        check_mover(!test.native.place_mover(id, actor, {origin, origin + 1.8}, true, work),
                    "Checked placement ignored embedded one-way geometry");
        // A departing jump must clear the former support instead of retaining
        // its support planes, penetration recovery or floor-follow cast.
        test.native.move_mover(id, actor, {0, 8});
        for (unsigned step = 0; step != 30; ++step)
            test.advance();
        const auto departing = test.native.mover_state(id, actor);
        check_mover(departing->position[1] > origin + 6 && !departing->support,
                    "One-way character departure retained stale support");
    }
}
inline void test_one_way_recovery(bool painted = false) {
    using namespace physics_detail;
    for (unsigned phase : {0u, 1u, 30u, 55u, 75u}) {
        auto settings = MoverGeometryFixture::settings();
        settings.gravity = {0, -9.81, 0};
        MoverNativeFixture original{settings};
        PhysicsBody2D platform;
        platform.one_way = true;
        original.add(one_way_floor(platform, painted), 0, painted ? -7 : 0);
        PhysicsBody2D body;
        body.motion = 2;
        const auto actor =
            original.add(inline_configuration_2d(body, 1, {.25f, 0}, {1, 1, 1}), 0, -2);
        original.synchronize();
        one_way_velocity(original, actor, {0, 8});
        for (unsigned step = 0; step != phase; ++step)
            original.advance();
        const auto image = original.image();
        if (phase == 30) {
            for (bool invalid_rotation : {false, true}) {
                auto broken = image.metadata;
                for (auto& entry : broken["mapping"])
                    if (entry["entity"] == Json(actor)) {
                        if (invalid_rotation)
                            entry["side_start"]["rotation"] = Json::array({0, 0});
                        else
                            entry["side_start"]["position"][0] = 1000001.;
                    }
                bool rejected = false;
                try {
                    original.native.restore(broken, image.bytes, original.desired, original.tick);
                } catch (const std::exception&) {
                    rejected = true;
                }
                check_mover(rejected, "Recovery admitted invalid one-way side history");
                check_mover_unchanged(original, image);
            }
        }
        MoverNativeFixture copy{settings};
        copy.desired = original.desired;
        copy.tick = original.tick;
        copy.native.restore(image.metadata, image.bytes, copy.desired, copy.tick);
        for (unsigned step = 0; step != 150; ++step) {
            original.advance();
            copy.advance();
            const auto a = original.native.state(original.id(actor), actor);
            const auto b = copy.native.state(copy.id(actor), actor);
            check_mover(a && b && a->position == b->position &&
                            a->linear_velocity == b->linear_velocity && a->active == b->active,
                        "Fresh one-way recovery lost fixed-boundary side history");
        }
        check_mover(std::abs(original.native.state(original.id(actor), actor)->position[1] - .35) <
                        .02,
                    "Fresh one-way recovery failed real landing");
    }
}
inline void test_one_way_character_recovery() {
    using namespace physics_detail;
    MoverNativeFixture original;
    PhysicsBody2D platform;
    platform.one_way = true;
    original.add(inline_configuration_2d(platform, 0, {10, .2f}, {1, 1, 1}), 0, 2);
    const auto actor = original.actor(0, 0, .7f);
    original.synchronize();
    double vertical = 8;
    for (unsigned i = 0; i < 45; ++i) {
        vertical -= 9.81 / 60;
        original.native.move_mover(original.id(actor), actor, {0, vertical});
        original.advance();
    }
    const auto image = original.image();
    MoverNativeFixture recovered;
    recovered.desired = original.desired;
    recovered.tick = original.tick;
    recovered.native.restore(image.metadata, image.bytes, recovered.desired, recovered.tick);
    for (unsigned i = 0; i < 150; ++i) {
        vertical -= 9.81 / 60;
        for (auto* test : {&original, &recovered}) {
            test->native.move_mover(test->id(actor), actor, {0, vertical});
            test->advance();
        }
        const auto a = original.native.mover_state(original.id(actor), actor);
        const auto b = recovered.native.mover_state(recovered.id(actor), actor);
        check_mover(a->position == b->position && a->velocity == b->velocity &&
                        bool(a->support) == bool(b->support),
                    "Fresh one-way character recovery changed support/motion");
        if (a->support && a->support->walkable && vertical <= 0)
            vertical = 0;
    }
    check_mover(
        std::abs(original.native.mover_state(original.id(actor), actor)->position[1] - 2.1) < .015,
        "Recovered one-way character did not land on the solid face");
}
inline void test_one_way_toggle() {
    using namespace physics_detail;
    MoverNativeFixture test;
    PhysicsBody2D platform;
    platform.one_way = true;
    const auto ground = test.add(inline_configuration_2d(platform, 0, {4, .2f}, {1, 1, 1}), 0, 0);
    PhysicsBody2D body;
    body.motion = 2;
    const auto actor = test.add(inline_configuration_2d(body, 1, {.25f, 0}, {1, 1, 1}), 0, -1);
    test.synchronize();
    const auto actor_epoch = test.native.state(test.id(actor), actor)->epoch;
    test.desired.at(test.id(ground)).config.body.one_way = false;
    one_way_velocity(test, actor, {0, 2});
    for (unsigned i = 0; i < 60; ++i)
        test.advance();
    check_mover(std::abs(test.native.state(test.id(actor), actor)->position[1] + .35) < .02,
                "Disabling one-way collision did not restore the ordinary underside");
    test.desired.at(test.id(ground)).config.body.one_way = true;
    one_way_velocity(test, actor, {0, 2});
    for (unsigned i = 0; i < 60; ++i)
        test.advance();
    const auto after = test.native.state(test.id(actor), actor);
    check_mover(after->position[1] > 1.5 && after->epoch == actor_epoch,
                "Enabling one-way collision retained solid response or replaced the actor");
}
} // namespace forge::test
