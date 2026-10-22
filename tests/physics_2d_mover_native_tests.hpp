#pragma once
#include "physics_2d_mover_motion_tests.hpp"

namespace forge::test {
struct MoverNativeFixture : MoverGeometryFixture {
    std::uint64_t tick = 0;
    EntityRef actor(double x, double y, float floor_y = -1) {
        PhysicsBody2D inner;
        inner.motion = 1;
        auto config = physics_detail::inline_configuration_2d(inner, 2, {.3f, .6f}, {1, 1, 1});
        config.mover_profile = physics_detail::MoverProfile2D{floor_y, .1f};
        return add(config, x, y);
    }
    std::uint64_t id(EntityRef ref) const {
        for (const auto& [id, value] : desired)
            if (value.ref == ref)
                return id;
        throw std::runtime_error("Missing native mover fixture identity");
    }
    void advance() {
        native.validate(desired, 1.f / 60);
        native.expire_events();
        std::vector<std::uint64_t> snaps;
        native.synchronize(desired, desired, 1.f / 60, {}, snaps);
        native.finish_targets(1.f / 60);
        native.step(1.f / 60, ++tick);
        adopt();
    }
    void adopt() {
        const auto poses = native.adopted_poses();
        native.adopt_events(tick);
        for (const auto& [id, pose] : poses)
            desired.at(id).pose = pose;
        native.accept_poses(poses);
    }
};
inline void test_mover_native_motion() {
    using namespace physics_detail;
    for (double offset : {0., -999990., 999990.}) {
        MoverNativeFixture test;
        test.add(inline_configuration_2d({}, 0, {20, 1}, {1, 1, 1}), offset, offset - .5);
        test.add(inline_configuration_2d({}, 0, {1, 6}, {1, 1, 1}), offset + 2.5, offset + 2);
        const auto actor =
            test.actor(offset - 1, offset, float(std::cos(50 * std::numbers::pi / 180)));
        test.synchronize();
        const auto id = test.id(actor);
        const auto epoch = test.native.mover_state(id, actor)->epoch;
        test.native.move_mover(id, actor, {3, -1});
        for (unsigned i = 0; i < 120; ++i)
            test.advance();
        const auto state = test.native.mover_state(id, actor);
        check_mover(state && std::abs(state->position[0] - offset - 1.705) < 1e-4 &&
                        std::abs(state->position[1] - offset + .005) < 1e-4 &&
                        state->intent == Double2{3, -1} &&
                        std::hypot(state->velocity[0], state->velocity[1]) < 1e-4 &&
                        state->epoch == epoch && test.desired.at(id).pose.translation.z == 7,
                    "Native controller lost feet origin, wall clipping, persistent intent or Z");
        const auto body = test.native.state(id, actor);
        check_mover(body && body->linear_velocity == Double2{} && body->angular_velocity == 0,
                    "Inner capsule moved through ordinary Kinematic velocity integration");
        test.native.move_mover(id, actor, {0, 3});
        for (unsigned i = 0; i < 20; ++i)
            test.advance();
        check_mover(test.native.mover_state(id, actor)->position[1] > offset + .99,
                    "Explicit upward controller intent did not leave the floor");
        test.native.move_mover(id, actor, {0, -3});
        for (unsigned i = 0; i < 40; ++i)
            test.advance();
        check_mover(std::abs(test.native.mover_state(id, actor)->position[1] - offset + .005) <
                        1e-4,
                    "Native controller did not land after replacing upward intent");
        const auto before = test.image();
        check_mover(!test.native.place_mover(id, actor, {offset + 2.5, offset}, true),
                    "Controller placement accepted solid wall containment");
        check_mover_unchanged(test, before);
        check_mover(test.native.place_mover(id, actor, {offset - 2, offset + 2}, true),
                    "Controller placement rejected a clear destination");
        test.adopt();
        check_mover(test.native.mover_state(id, actor)->intent == Double2{} &&
                        test.native.mover_state(id, actor)->velocity == Double2{} &&
                        test.desired.at(id).pose.translation.z == 7,
                    "Clear placement retained controller motion or changed authored Z");
    }
}
inline void test_mover_native_filters() {
    using namespace physics_detail;
    for (unsigned profile = 0; profile != 4; ++profile) {
        MoverNativeFixture test;
        PhysicsBody2D wall;
        wall.layer = 1;
        wall.mask = profile == 0 ? 0 : 1;
        wall.sensor = profile == 2;
        test.add(inline_configuration_2d(wall, 0, {1, 4}, {1, 1, 1}), .5, .6);
        const auto ref = test.actor(-2, 0);
        const auto id = test.id(ref);
        test.desired.at(id).config.body.mask = profile == 3 ? 1 : 2;
        test.synchronize();
        test.native.move_mover(id, ref, {6, 0});
        for (unsigned i = 0; i < 30; ++i)
            test.advance();
        const auto result = test.native.mover_state(id, ref);
        const double expected = profile == 1 ? -.295 : 1;
        check_mover(result && std::abs(result->position[0] - expected) < 1e-4,
                    "Native controller ignored mutual body masks or blocked on a sensor");
    }
}
inline void test_mover_native_admission() {
    using namespace physics_detail;
    MoverNativeFixture test;
    const auto ref = test.actor(0, 0);
    test.synchronize();
    const auto id = test.id(ref);
    for (unsigned invalid = 0; invalid != 11; ++invalid) {
        const auto before = test.image();
        auto candidate = test.desired;
        auto& body = candidate.at(id);
        switch (invalid) {
        case 0:
            body.config.mover_profile->minimum_floor_y = std::numeric_limits<float>::quiet_NaN();
            break;
        case 1:
            body.config.body.motion = 2;
            break;
        case 2:
            body.config.body.sensor = true;
            break;
        case 3:
            body.pose.rotation = rotation_about_axis({0, 0, 1}, 10);
            break;
        case 4:
            body.pose.scale = {2, 1, 1};
            break;
        case 5:
            body.pose.scale = {1, -1, 1};
            break;
        case 6:
            body.pose.translation.x = 1;
            break;
        case 7:
            body.config.dimensions[0] = .01f;
            break;
        case 8:
            body.config.mover_profile->floor_probe = -1;
            break;
        case 9:
            body.config.mover_profile->floor_probe = 1.01f;
            break;
        case 10:
            body.config.mover_profile->floor_probe = std::numeric_limits<float>::quiet_NaN();
            break;
        }
        bool rejected = false;
        try {
            test.native.validate(candidate, 1.f / 60);
        } catch (const std::exception&) {
            rejected = true;
        }
        check_mover(rejected, "Invalid controller profile or authored pose write was admitted");
        check_mover_unchanged(test, before);
    }
    const auto before = test.image();
    bool rejected = false;
    BodyCommand command;
    command.ref = ref;
    command.planar = true;
    command.kind = BodyCommandKind::Teleport;
    command.p = {3, 4, 7};
    std::vector<std::uint64_t> snaps;
    try {
        test.native.apply(id, command, snaps);
    } catch (const std::exception&) {
        rejected = true;
    }
    check_mover(rejected && snaps.empty(), "Body command acquired controller transform authority");
    check_mover_unchanged(test, before);
    test.native.move_mover(id, ref, {1, 0});
    const auto prior_epoch = test.native.mover_state(id, ref)->epoch;
    test.desired.at(id).config.mover_profile->minimum_floor_y = .7f;
    test.synchronize();
    check_mover(test.native.mover_state(id, ref)->intent == Double2{1, 0} &&
                    test.native.mover_state(id, ref)->epoch != prior_epoch,
                "Controller setting edit lost motion or retained the retired epoch");
    test.advance();
    const auto previous = test.native.mover_state(id, ref)->position;
    test.desired.at(id).config.mover_profile.reset();
    test.synchronize();
    check_mover(!test.native.mover_state(id, ref),
                "Rigid representation retained controller state");
    test.advance();
    check_mover(test.native.state(id, ref)->position == previous,
                "Controller velocity leaked into a replacement ordinary Kinematic body");
}
inline void test_mover_native_order() {
    using namespace physics_detail;
    MoverNativeFixture first, reordered;
    const auto a = first.actor(-1, 0), b = first.actor(1, 0);
    reordered.scene = first.scene;
    reordered.desired.emplace(42, first.desired.at(first.id(b)));
    reordered.desired.emplace(99, first.desired.at(first.id(a)));
    first.synchronize();
    reordered.synchronize();
    for (auto* test : {&first, &reordered}) {
        test->native.move_mover(test->id(a), a, {2, 0});
        test->native.move_mover(test->id(b), b, {-2, 0});
        test->native.expire_events();
        test->native.step(.5f, ++test->tick);
        test->adopt();
    }
    const auto left = first.native.mover_state(first.id(a), a);
    const auto right = first.native.mover_state(first.id(b), b);
    check_mover(left && right && right->position[0] - left->position[0] >= .5949 &&
                    std::abs((a < b ? left : right)->position[0]) < 1e-5,
                "Simultaneous controller movement overlapped or ignored canonical order");
    for (auto ref : {a, b}) {
        const auto original = first.native.mover_state(first.id(ref), ref);
        const auto copy = reordered.native.mover_state(reordered.id(ref), ref);
        check_mover(original && copy && original->position == copy->position &&
                        original->velocity == copy->velocity,
                    "Controller movement depended on transient ECS entity order");
    }
}
inline void test_mover_native_candidate_geometry() {
    using namespace physics_detail;
    for (double offset : {0., -999990., 999990.}) {
        for (unsigned kind = 0; kind != 4; ++kind) {
            MoverNativeFixture test;
            const auto actor = test.actor(offset, offset);
            const auto id = test.id(actor);
            Body2DConfiguration obstacle;
            if (kind == 3) {
                obstacle.shape = 3;
                obstacle.parts = std::make_shared<const std::vector<BoxPart2D>>(
                    std::vector<BoxPart2D>{{{0, 0}, {4, 4}}});
            } else {
                constexpr std::array<std::array<float, 2>, 3> dimensions{
                    {{4, 4}, {2, 0}, {.3f, .6f}}};
                obstacle = inline_configuration_2d({}, kind, dimensions[kind], {1, 1, 1});
            }
            test.add(obstacle, offset, offset + .6);
            bool rejected = false;
            try {
                test.native.validate(test.desired);
            } catch (const std::exception& error) {
                rejected = std::string_view(error.what()).find("overlaps solid geometry") !=
                           std::string_view::npos;
            }
            check_mover(
                rejected && !test.native.realized(),
                "New controller/obstacle containment was admitted or mutated the native world");
            // Increasing a feet-origin radius also raises its lower core. Use
            // a known horizontal gap, rather than assuming a huge radius must
            // intersect a short capsule that it can instead arch above.
            const double clear_x = offset - (kind == 2 ? .7 : 2.4);
            test.desired.at(id).pose.translation.x = clear_x;
            test.synchronize();
            const auto before = test.image();
            auto candidate = test.desired;
            candidate.at(id).config.dimensions[0] = .6f;
            rejected = false;
            try {
                test.native.validate(candidate);
            } catch (const std::exception&) {
                rejected = true;
            }
            check_mover(rejected,
                        "Controller shape enlargement ignored unchanged candidate obstacles");
            check_mover_unchanged(test, before);
            candidate = test.desired;
            candidate.at(id == 1 ? 2 : 1).pose.translation.x = clear_x;
            rejected = false;
            try {
                test.native.validate(candidate);
            } catch (const std::exception&) {
                rejected = true;
            }
            check_mover(rejected, "Edited obstacle acquired an unchanged controller's pose");
            check_mover_unchanged(test, before);
        }
    }
}
inline void test_mover_native_recovery() {
    using namespace physics_detail;
    MoverNativeFixture test;
    const auto actor = test.actor(-2, 1);
    const auto wall = test.add(inline_configuration_2d({}, 0, {1, 4}, {1, 1, 1}), .5, 1);
    test.synchronize();
    test.native.move_mover(test.id(actor), actor, {2, 0});
    test.advance();
    const auto image = test.image();
    Desired2D rebound;
    rebound.emplace(501, test.desired.at(test.id(actor)));
    rebound.emplace(41, test.desired.at(test.id(wall)));
    Native2D restored(test.config);
    restored.restore(image.metadata, image.bytes, rebound, test.tick);
    const auto original = test.native.mover_state(test.id(actor), actor);
    const auto copy = restored.mover_state(501, actor);
    check_mover(original && copy && original->position == copy->position &&
                    original->intent == copy->intent && original->velocity == copy->velocity &&
                    original->epoch == copy->epoch && !restored.mover_state(test.id(actor), actor),
                "Fresh controller recovery lost copied motion, epoch or canonical rebinding");
    restored.expire_events();
    restored.step(1.f / 60, test.tick + 1);
    test.advance();
    const auto continued = restored.mover_state(501, actor);
    check_mover(continued &&
                    continued->position == test.native.mover_state(test.id(actor), actor)->position,
                "Recovered controller continuation diverged from uninterrupted movement");
    for (unsigned corruption = 0; corruption != 3; ++corruption) {
        auto metadata = image.metadata;
        for (auto& entry : metadata["mapping"])
            if (entry["entity"] == Json(actor)) {
                if (corruption == 0)
                    entry["mover"]["intent"] = {1000001, 0};
                else if (corruption == 1)
                    entry["configuration"].erase("mover_profile");
                else
                    entry["mover"].erase("velocity");
            }
        Native2D candidate(test.config);
        bool rejected = false;
        try {
            candidate.restore(metadata, image.bytes, rebound, test.tick - 1);
        } catch (const std::exception&) {
            rejected = true;
        }
        check_mover(rejected && !candidate.realized() && candidate.size() == 0,
                    "Invalid controller recovery published a partial native candidate");
    }
}
inline void test_mover_native_budget() {
    using namespace physics_detail;
    for (bool together : {false, true}) {
        MoverNativeFixture test;
        Body2DConfiguration floor;
        floor.shape = 3;
        auto parts = std::make_shared<std::vector<BoxPart2D>>();
        for (float x : {-3.f, 3.f})
            for (unsigned i = 0; i < 920; ++i)
                parts->push_back({{x, -.5f}, {1, 1}});
        floor.parts = parts;
        test.add(floor, 0, 0);
        test.actor(-3, 0);
        if (together)
            test.actor(3, 0);
        test.synchronize();
        std::string error;
        try {
            test.advance();
        } catch (const std::exception& failure) {
            error = failure.what();
        }
        if (!together) {
            check_mover(error.empty(), "One bounded controller exhausted a step-wide work budget");
            continue;
        }
        check_mover(error.find("work capacity 16384") != std::string::npos,
                    "Controllers did not share the composed step-wide query work budget");
        bool faulted = false;
        try {
            (void)test.native.adopted_poses();
        } catch (const std::exception&) {
            faulted = true;
        }
        check_mover(faulted, "A failed controller step allowed partial pose adoption");
    }
}
} // namespace forge::test
