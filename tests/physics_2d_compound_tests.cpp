#include "physics_2d.hpp"
#include "physics_2d_mover_motion_tests.hpp"
#include "physics_2d_mover_native_tests.hpp"
#include "physics_2d_mover_support_tests.hpp"
#include "physics_2d_mover_tests.hpp"
#include <cmath>
#include <iostream>

using namespace forge;
using namespace forge::physics_detail;
namespace {
void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F> void reject(F&& action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid compound2D candidate was accepted");
}
struct Fixture {
    PhysicsConfig config;
    Native2D native{config};
    Desired2D desired;
    DesiredJoints2D joints;
    std::uint64_t tick = 0;
    explicit Fixture(unsigned rectangles = 65, double offset = 0, bool sensor = false,
                     bool reverse = false) {
        const auto scene = AssetId::generate();
        auto parts = std::make_shared<std::vector<BoxPart2D>>();
        for (unsigned i = 0; i < rectangles; ++i)
            parts->push_back({{float(i) + .5f, -.5f}, {1, 1}});
        Body2DConfiguration floor;
        floor.shape = 3;
        floor.parts = std::move(parts);
        LocalTransform pose;
        pose.translation = {offset, offset, 7};
        desired.emplace(reverse ? 2 : 1, Body2DDesired{{scene, EntityId::generate()}, floor, pose});
        PhysicsBody2D body;
        body.motion = 2;
        body.sensor = sensor;
        body.gravity_factor = sensor ? 0 : 1;
        pose.translation.x += .5;
        pose.translation.y += sensor ? -.5 : 2;
        desired.emplace(reverse ? 1 : 2,
                        Body2DDesired{{scene, EntityId::generate()},
                                      inline_configuration_2d(body, 1, {.25f, 0}, {1, 1, 1}),
                                      pose});
        synchronize();
    }
    void synchronize() {
        native.validate(desired, 1.f / 60, {}, {}, joints);
        native.expire_events();
        std::vector<std::uint64_t> discontinuities;
        native.synchronize(desired, desired, 1.f / 60, {}, discontinuities, joints);
        native.finish_targets(1.f / 60);
    }
    void advance(unsigned count = 1) {
        for (unsigned i = 0; i < count; ++i) {
            synchronize();
            native.step(1.f / 60, ++tick);
            const auto poses = native.adopted_poses();
            native.adopt_events(tick);
            for (const auto& [id, pose] : poses)
                desired.at(id).pose = pose;
            native.accept_poses(poses);
        }
    }
};
void geometry_queries_and_recovery(double offset) {
    Fixture test(65, offset);
    check(test.native.size() == 2 && test.native.shape_size() == 66,
          "Compound collider created per-cell bodies or omitted shapes");
    const auto floor = test.desired.at(1).ref, actor = test.desired.at(2).ref;
    test.advance(90);
    const auto state = test.native.state(2, actor);
    check(state && std::abs(state->position[1] - offset - .25) < .01,
          "Dynamic body did not land on the compound floor");
    PhysicsOverlap2D query;
    query.shape = PhysicsOverlap2D::Shape::Box;
    query.origin = {offset + 32, offset - .5};
    query.dimensions = {64, .5};
    const auto overlaps = test.native.overlap(query, {});
    check(overlaps == std::vector<EntityRef>{floor},
          "Compound overlap did not deduplicate its body reference");
    const auto hit = test.native.raycast({offset + 32, offset + 1}, {0, -3}, {});
    check(hit && hit->entity == floor && std::abs(hit->fraction - 1. / 3) < .001,
          "Compound closest ray lost geometry or its actor binding");
    const auto floor_epoch = test.native.state(1, floor)->epoch;
    test.desired.at(1).config.parts =
        std::make_shared<const std::vector<BoxPart2D>>(*test.desired.at(1).config.parts);
    test.synchronize();
    check(test.native.state(1, floor)->epoch == floor_epoch,
          "Equal geometry in a new immutable array rebuilt the native body");
    const auto image = test.native.checkpoint(test.desired, 2 * 1024 * 1024);
    Desired2D remapped{{101, test.desired.at(1)}, {202, test.desired.at(2)}};
    Native2D restored(test.config);
    restored.restore(image.metadata, image.bytes, remapped, test.tick);
    check(restored.shape_size() == 66 && restored.event_count() == 0 &&
              restored.state(202, actor) == test.native.state(2, actor),
          "Fresh compound recovery lost shapes/state or replayed old observations");
    for (unsigned i = 0; i < 120; ++i) {
        test.advance();
        restored.validate(remapped);
        restored.expire_events();
        std::vector<std::uint64_t> discontinuities;
        restored.synchronize(remapped, remapped, 1.f / 60, {}, discontinuities);
        restored.finish_targets(1.f / 60);
        restored.step(1.f / 60, test.tick);
        const auto poses = restored.adopted_poses();
        restored.adopt_events(test.tick);
        for (const auto& [id, pose] : poses)
            remapped.at(id).pose = pose;
        restored.accept_poses(poses);
        check(restored.state(202, actor) == test.native.state(2, actor),
              "Fresh compound recovery diverged after remapped native binding");
    }
    const auto current = test.native.checkpoint(test.desired, 2 * 1024 * 1024);
    auto mismatched = test.desired;
    auto changed = std::make_shared<std::vector<BoxPart2D>>(*mismatched.at(1).config.parts);
    changed->at(0).dimensions[1] = .5f;
    mismatched.at(1).config.parts = std::move(changed);
    auto metadata = current.metadata;
    for (auto& row : metadata["mapping"])
        if (row.at("entity").get<EntityRef>() == floor)
            row["configuration"] = mismatched.at(1).config.json();
    Native2D rejected(test.config);
    reject([&] { rejected.restore(metadata, current.bytes, mismatched, test.tick); });
    check(!rejected.realized() && rejected.size() == 0,
          "Mismatched compound native geometry published part of a candidate");
    rejected.restore(current.metadata, current.bytes, test.desired, test.tick);
    check(rejected.state(2, actor) == test.native.state(2, actor),
          "Rejected compound recovery contaminated a subsequent valid restoration");
    auto moved = std::make_shared<std::vector<BoxPart2D>>(*test.desired.at(1).config.parts);
    for (auto& part : *moved)
        part.center[0] += 100;
    test.desired.at(1).config.parts = std::move(moved);
    test.advance();
    const auto next_epoch = test.native.state(1, floor)->epoch;
    bool retired = false;
    for (std::size_t i = 0; i < test.native.event_count(); ++i) {
        const auto event = *test.native.event(i);
        if (!event.begin) {
            if (event.first == floor && event.first_epoch == floor_epoch)
                retired = !test.native.current(1, floor, event.first_epoch);
            if (event.second == floor && event.second_epoch == floor_epoch)
                retired = !test.native.current(1, floor, event.second_epoch);
        }
    }
    check(retired && next_epoch != floor_epoch,
          "Compound replacement retargeted old contact Ends through the new realization");
}
void capacity_and_invalid_geometry() {
    Fixture test(4095);
    test.advance();
    const auto image = test.native.checkpoint(test.desired, 2 * 1024 * 1024);
    check(test.native.shape_size() == 4096 && image.bytes.size() < 2 * 1024 * 1024,
          "Admitted compound shape capacity did not fit the tested solver profile");
    const auto before = test.native.state(1, test.desired.at(1).ref);
    check(bool(before), "Admitted compound body has no copied state");
    auto bad = test.desired;
    auto excessive = std::make_shared<std::vector<BoxPart2D>>(*bad.at(1).config.parts);
    excessive->push_back({{4096.5f, -.5f}, {1, 1}});
    bad.at(1).config.parts = excessive;
    reject([&] { test.native.validate(bad); });
    check(test.native.shape_size() == 4096 &&
              test.native.state(1, test.desired.at(1).ref) == before,
          "Shape-capacity preflight changed the current native realization");
    for (const auto invalid :
         {BoxPart2D{{0, 0}, {0, 1}}, BoxPart2D{{10000, 0}, {1, 1}}, BoxPart2D{{0, 0}, {1, 1001}}}) {
        bad = test.desired;
        bad.at(1).config.parts = std::make_shared<const std::vector<BoxPart2D>>(1, invalid);
        reject([&] { test.native.validate(bad); });
    }
    for (unsigned motion : {1u, 2u}) {
        bad = test.desired;
        bad.at(1).config.body.motion = motion;
        reject([&] { test.native.validate(bad); });
    }
    bad = test.desired;
    bad.at(1).config.body.sensor = true;
    reject([&] { test.native.validate(bad); });
    bad = test.desired;
    bad.at(1).config.parts.reset();
    reject([&] { test.native.validate(bad); });
    test.advance();
    check(test.native.state(1, test.desired.at(1).ref)->epoch == before->epoch,
          "Rejected compound candidates prevented valid unchanged continuation");
}
void sensor_retirement() {
    Fixture test(4, 0, true);
    const auto floor = test.desired.at(1).ref, sensor = test.desired.at(2).ref;
    const auto epoch = test.native.state(1, floor)->epoch;
    test.advance();
    bool began = false;
    for (std::size_t i = 0; i < test.native.event_count(); ++i) {
        const auto event = *test.native.event(i);
        began |= event.sensor && event.begin && event.first == sensor && event.second == floor;
    }
    check(began, "Compound visitor did not bind the sensor Begin to its Map actor");
    auto moved = std::make_shared<std::vector<BoxPart2D>>(*test.desired.at(1).config.parts);
    for (auto& part : *moved)
        part.center[0] += 100;
    test.desired.at(1).config.parts = std::move(moved);
    test.advance();
    bool ended = false;
    for (std::size_t i = 0; i < test.native.event_count(); ++i) {
        const auto event = *test.native.event(i);
        ended |= event.sensor && !event.begin && event.first == sensor && event.second == floor &&
                 event.second_epoch == epoch && !test.native.current(1, floor, epoch);
    }
    check(ended, "Compound sensor retirement lost the old visitor's body epoch");
}
void connected_collision_and_recovery() {
    // Actor sorts before the compound ground, exercising exclusions against all
    // of the second body's shapes rather than only an arbitrary first shape.
    Fixture test(65, 0, false, true);
    test.advance(90);
    const auto floor = test.desired.at(2).ref, actor = test.desired.at(1).ref;
    const auto epoch = test.native.state(2, floor)->epoch;
    PhysicsJoint2D joint;
    joint.body_a = floor;
    joint.body_b = actor;
    joint.anchor_a_x = .5f;
    joint.distance = .25f;
    joint.collide_connected = false;
    test.joints.emplace(
        3, Joint2DDesired{{floor.scene, EntityId::generate()}, joint, 2, 1, {.5f, 0}, {0, 0}});
    test.advance();
    bool ended = false;
    for (std::size_t i = 0; i < test.native.event_count(); ++i) {
        const auto event = *test.native.event(i);
        ended |= !event.sensor && !event.begin &&
                 ((event.first == floor && event.second == actor) ||
                  (event.first == actor && event.second == floor));
    }
    check(ended && test.native.state(2, floor)->epoch == epoch && test.native.joint_size() == 1,
          "Connected collision exclusion lost compound contacts or rebuilt unchanged geometry");
    const auto image = test.native.checkpoint(test.desired, 2 * 1024 * 1024, test.joints);
    Native2D restored(test.config);
    restored.restore(image.metadata, image.bytes, test.desired, test.tick, test.joints);
    check(restored.shape_size() == 66 && restored.joint_size() == 1 &&
              restored.state(1, actor) == test.native.state(1, actor),
          "Fresh recovery failed to bind a joint to its compound Static endpoint");
    test.joints.clear();
    bool began = false;
    for (unsigned tick = 0; tick < 3 && !began; ++tick) {
        test.advance();
        for (std::size_t i = 0; i < test.native.event_count(); ++i) {
            const auto event = *test.native.event(i);
            began |= !event.sensor && event.begin &&
                     ((event.first == floor && event.second == actor) ||
                      (event.first == actor && event.second == floor));
        }
    }
    check(began && test.native.state(2, floor)->epoch == epoch,
          "Removing the final exclusion did not rediscover the unmoved compound pair");
}
} // namespace
int main() {
    try {
        forge::test::test_mover_geometry();
        forge::test::test_mover_directions();
        forge::test::test_mover_encroachment();
        forge::test::test_mover_capacity();
        forge::test::test_mover_penetration();
        std::cout << "Checked mover cast/directions/capacity/penetration PASS\n";
        forge::test::test_mover_motion();
        forge::test::test_mover_motion_surfaces();
        forge::test::test_mover_motion_sleep();
        forge::test::test_mover_motion_limits();
        std::cout << "Checked mover motion/feasibility/shared-budget PASS\n";
        forge::test::test_mover_native_motion();
        forge::test::test_mover_native_filters();
        forge::test::test_mover_native_admission();
        forge::test::test_mover_native_candidate_geometry();
        forge::test::test_mover_native_order();
        forge::test::test_mover_native_recovery();
        forge::test::test_mover_native_budget();
        std::cout << "Native mover ownership/filter/order/recovery/shared-budget PASS\n";
        forge::test::test_mover_support_slopes();
        forge::test::test_mover_support_velocity_and_lifetime();
        forge::test::test_mover_support_curves();
        forge::test::test_mover_support_recovery();
        forge::test::test_mover_support_fixed_budget();
        std::cout << "Native mover support/floor-follow/lifetime/recovery PASS\n";
        for (double offset : {0., 999000., -999000.})
            geometry_queries_and_recovery(offset);
        capacity_and_invalid_geometry();
        sensor_retirement();
        connected_collision_and_recovery();
        std::cout << "Compound2D geometry/query/retirement/recovery/capacity PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
