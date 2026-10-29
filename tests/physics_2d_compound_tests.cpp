#include "builtins.hpp"
#include "physics_2d.hpp"
#include "physics_2d_mover_motion_tests.hpp"
#include "physics_2d_mover_native_tests.hpp"
#include "physics_2d_mover_support_tests.hpp"
#include "physics_2d_mover_tests.hpp"
#include "physics_2d_one_way_tests.hpp"
#include "physics_2d_platform_carry_tests.hpp"
#include "physics_2d_stance_tests.hpp"
#include "physics_2d_step_tests.hpp"
#include "physics_2d_surface_response_tests.hpp"
#include "physics_debug.hpp"
#include "tile_map_cook.hpp"
#include "tile_set_cook.hpp"
#include <cmath>
#include <iostream>
#include <tuple>

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
void tile_polygon_projection_and_recovery() {
    auto source = TileSetSource::create(AssetId::generate());
    source.document["next_id"] = 4;
    source.document["images"].push_back({{"id", 1},
                                         {"name", "Image"},
                                         {"material", AssetId::generate()},
                                         {"grid",
                                          {{"columns", 1},
                                           {"rows", 1},
                                           {"margin_x", 0},
                                           {"margin_y", 0},
                                           {"spacing_x", 0},
                                           {"spacing_y", 0}}}});
    for (std::uint64_t id : {2, 3})
        source.document["tiles"].push_back(
            {{"id", id}, {"name", "Tile"}, {"image", 1}, {"frame", 0}});
    source.set_collision(2, {{"kind", "convex"}, {"points", {{0., 0.}, {1., 0.}, {0., 1.}}}});
    source.set_collision(3, {{"kind", "none"}});
    const auto definitions = decode_cooked_tile_set(cook_tile_set(source), source.asset());
    for (unsigned flags = 0; flags < 8; ++flags)
        for (float x_sign : {-1.f, 1.f})
            for (float y_sign : {-1.f, 1.f})
                for (double angle : {0., .35, 1.5707963267948966}) {
                    auto value = TileMapValue::create(AssetId::generate(), source.asset());
                    auto metadata = value.metadata();
                    metadata["cell_size"] = {4., 1.};
                    value = value.with_metadata(std::move(metadata));
                    const std::array<TileMapCellEdit, 2> edits{
                        {{1, {0, -1}, {2, flags}}, {1, {1, -1}, {3, 0}}}};
                    value = value.paint(edits);
                    const auto map = decode_cooked_tile_map(cook_tile_map(value), value.asset());
                    const LocalScale scale{x_sign * 2, y_sign, 1};
                    PhysicsBody2D map_body;
                    map_body.motion = flags % 2;
                    const auto config =
                        tile_map_configuration_2d(map_body, map, 1, scale, &definitions);
                    check(config.parts->empty() && config.polygons &&
                              config.polygons->size() == 1 && config.polygons->at(0).count == 3,
                          "Tile polygons/None did not reach compound configuration");
                    Fixture test(1, 1000);
                    test.desired.at(1).config = config;
                    test.desired.at(1).pose.rotation = {0, 0, float(std::sin(angle / 2)),
                                                        float(std::cos(angle / 2))};
                    test.desired.at(2).ref.entity = EntityId::generate();
                    test.desired.at(2).pose.translation.y += 100;
                    test.advance();
                    check(test.native.shape_size() == 2,
                          "Tile polygon allocated a per-cell body or kept None");
                    const auto& polygon = config.polygons->at(0);
                    double cx = 0, cy = 0;
                    for (unsigned i = 0; i < polygon.count; ++i) {
                        cx += polygon.points[i][0] / 3.;
                        cy += polygon.points[i][1] / 3.;
                    }
                    const double x = 1000 + std::cos(angle) * cx - std::sin(angle) * cy;
                    const double y = 1000 + std::sin(angle) * cx + std::cos(angle) * cy;
                    PhysicsOverlap2D overlap;
                    overlap.origin = {x, y};
                    overlap.dimensions = {.01, 0};
                    check(test.native.overlap(overlap, {}) ==
                              std::vector<EntityRef>{test.desired.at(1).ref},
                          "Signed/rotated/flipped polygon lost native geometry or canonical query "
                          "binding");
                    const auto hit = test.native.raycast({x, y + 10}, {0, -20}, {});
                    check(hit && hit->entity == test.desired.at(1).ref,
                          "Ray missed signed/rotated Tile Set polygon");
                    TileMapRenderer renderer;
                    renderer.map = {map.asset()};
                    Json components{
                        {"forge.physics_body_2d", Json::object()},
                        {"forge.tile_map_renderer", detail::encode_builtin_value(renderer)},
                        {"forge.tile_map_collider_2d", {{"layer", 1}}}};
                    const auto guide = prepare_physics_debug(components, scale, {}, false, 8192,
                                                             &map, &definitions);
                    check(guide.lines.size() == 3 && !guide.truncated,
                          "Shared Tile Set outline used full-cell rectangles");
                    const auto limited =
                        prepare_physics_debug(components, scale, {}, false, 2, &map, &definitions);
                    check(limited.lines.size() == 2 && limited.truncated,
                          "Polygon outline ignored its line cap");
                    auto one_way = PhysicsBody2D{};
                    one_way.one_way = true;
                    reject(
                        [&] { tile_map_configuration_2d(one_way, map, 1, scale, &definitions); });
                    const auto image = test.native.checkpoint(test.desired, 2 * 1024 * 1024);
                    Native2D restored(test.config);
                    Desired2D remapped{{101, test.desired.at(1)}, {202, test.desired.at(2)}};
                    restored.restore(image.metadata, image.bytes, remapped, test.tick);
                    check(restored.shape_size() == 2 &&
                              restored.state(202, remapped.at(202).ref) ==
                                  test.native.state(2, test.desired.at(2).ref),
                          "Fresh-world Tile Set polygon recovery lost native geometry/state");
                }
    {
        source.set_collision(2, {{"kind", "convex"},
                                 {"points",
                                  {{0., .25},
                                   {.25, 0.},
                                   {.75, 0.},
                                   {1., .25},
                                   {1., .75},
                                   {.75, 1.},
                                   {.25, 1.},
                                   {0., .75}}}});
        const auto octagon = decode_cooked_tile_set(cook_tile_set(source), source.asset());
        auto value = TileMapValue::create(AssetId::generate(), source.asset());
        const std::array<TileMapCellEdit, 1> edits{{{1, {0, 0}, {2, 7}}}};
        value = value.paint(edits);
        const auto map = decode_cooked_tile_map(cook_tile_map(value), value.asset());
        Fixture test(1);
        test.desired.at(1).config = tile_map_configuration_2d({}, map, 1, {-2, -1, 0}, &octagon);
        test.advance();
        check(test.desired.at(1).config.polygons->at(0).count == 8 && test.native.shape_size() == 2,
              "Maximum tile contour lost vertices during native admission");
        const auto image = test.native.checkpoint(test.desired, 2 * 1024 * 1024);
        Native2D copy(test.config);
        copy.restore(image.metadata, image.bytes, test.desired, test.tick);
        check(copy.state(1, test.desired.at(1).ref) == test.native.state(1, test.desired.at(1).ref),
              "Maximum tile contour failed complete geometry recovery");
        auto mismatch = test.desired;
        auto changed =
            std::make_shared<std::vector<PolygonPart2D>>(*mismatch.at(1).config.polygons);
        changed->at(0).points[0][0] -= .01f;
        mismatch.at(1).config.polygons = changed;
        auto metadata = image.metadata;
        for (auto& row : metadata["mapping"])
            if (row.at("entity").get<EntityRef>() == test.desired.at(1).ref)
                row["configuration"] = mismatch.at(1).config.json();
        Native2D rejected(test.config);
        reject([&] { rejected.restore(metadata, image.bytes, mismatch, test.tick); });
        check(!rejected.realized() && rejected.size() == 0,
              "Mismatched eight-vertex recovery published a native candidate");
    }
    for (bool sensor : {false, true}) {
        Fixture test(1);
        auto polygons = std::make_shared<std::vector<PolygonPart2D>>();
        polygons->push_back({{{{-2, -1}, {4, -1}, {4, 0}}}, 3});
        auto& config = test.desired.at(1).config;
        config.parts.reset();
        config.polygons = polygons;
        config.body.sensor = sensor;
        config.body.motion = sensor ? 1 : 0;
        test.desired.at(2).config.body.gravity_factor = 1;
        test.synchronize();
        const auto epoch = test.native.state(1, test.desired.at(1).ref)->epoch;
        config.polygons = std::make_shared<const std::vector<PolygonPart2D>>(*polygons);
        test.synchronize();
        check(test.native.state(1, test.desired.at(1).ref)->epoch == epoch,
              "Copied equal polygons rebuilt an unchanged native body");
        bool began = false;
        for (unsigned i = 0; i < 65; ++i) {
            test.advance();
            for (std::size_t e = 0; e < test.native.event_count(); ++e) {
                const auto event = test.native.event(e);
                began |= event && event->begin && event->sensor == sensor;
            }
        }
        check(began, "Custom polygon did not produce its physical contact/sensor observation");
        const auto image = test.native.checkpoint(test.desired, 2 * 1024 * 1024);
        Native2D restored(test.config);
        auto remapped = test.desired;
        restored.restore(image.metadata, image.bytes, remapped, test.tick);
        for (unsigned i = 0; i < 60; ++i) {
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
            check(restored.state(2, remapped.at(2).ref) ==
                      test.native.state(2, test.desired.at(2).ref),
                  "Polygon contacts diverged after fresh native recovery");
        }
        auto invalid = test.desired;
        auto bad = std::make_shared<std::vector<PolygonPart2D>>(*config.polygons);
        bad->at(0).points[1] = bad->at(0).points[0];
        invalid.at(1).config.polygons = bad;
        reject([&] { test.native.validate(invalid); });
        test.advance();
        check(test.native.state(1, test.desired.at(1).ref)->epoch == epoch,
              "Rejected polygon candidate changed the active collider");
    }
}
void tile_polygon_controller() {
    forge::test::MoverNativeFixture test;
    Body2DConfiguration floor;
    floor.shape = 3;
    floor.polygons = std::make_shared<const std::vector<PolygonPart2D>>(
        std::vector<PolygonPart2D>{{{{{-5, -1}, {5, -1}, {5, 1}}}, 3}});
    const auto support = test.add(floor, 0, 0);
    const auto actor = test.actor(0, 1.5, .7f);
    const auto id = test.id(actor);
    test.synchronize();
    test.native.move_mover(id, actor, {0, -3});
    for (unsigned step = 0; step < 60; ++step)
        test.advance();
    const auto state = test.native.mover_state(id, actor);
    check(state && state->support && state->support->entity == support &&
              state->support->walkable &&
              std::abs(state->support->normal[1] - 1 / std::sqrt(1.04)) < 1e-5,
          "Character support did not use the custom polygon slope");
    std::size_t work = 0;
    check(!test.native.place_mover(id, actor, {0, -.5}, true, work),
          "Checked character placement accepted polygon containment");
    test.native.move_mover(id, actor, {2, -3});
    const auto image = test.image();
    Native2D recovered(test.config);
    auto remapped = test.desired;
    recovered.restore(image.metadata, image.bytes, remapped, test.tick);
    for (unsigned step = 0; step < 30; ++step) {
        test.advance();
        recovered.validate(remapped);
        recovered.expire_events();
        std::vector<std::uint64_t> discontinuities;
        recovered.synchronize(remapped, remapped, 1.f / 60, {}, discontinuities);
        recovered.finish_targets(1.f / 60);
        recovered.step(1.f / 60, test.tick);
        const auto poses = recovered.adopted_poses();
        recovered.adopt_events(test.tick);
        for (const auto& [entity, pose] : poses)
            remapped.at(entity).pose = pose;
        recovered.accept_poses(poses);
        const auto original = test.native.mover_state(id, actor),
                   copy = recovered.mover_state(id, actor);
        const auto values = [](const MoverState2D& state) {
            return std::tie(state.position, state.intent, state.velocity, state.epoch,
                            state.support, state.placement_blocked, state.crouched,
                            state.shape_change_blocked);
        };
        check(original && copy && values(*original) == values(*copy),
              "Character polygon support diverged after fresh native recovery");
    }
    check(test.native.mover_state(id, actor)->position[0] > .25,
          "Character did not climb the admitted convex ramp");
}
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
    const auto all = test.native.raycast_all({offset - 1, offset - .5}, {67, 0}, {});
    check(all.size() == 1 && all[0].entity == floor && std::abs(all[0].fraction - 1. / 67) < .001,
          "Multi-hit compound ray duplicated parts or lost the nearest surface");
    const std::array<EntityRef, 1> excluded{floor};
    const PhysicsQueryFilter filter{UINT32_MAX, true, excluded};
    check(test.native.overlap(query, filter).empty() &&
              !test.native.raycast({offset + 32, offset + 1}, {0, -3}, filter) &&
              test.native.raycast_all({offset - 1, offset - .5}, {67, 0}, filter).empty(),
          "Canonical exclusion retained a part of a compound body");
    PhysicsSweep2D sweep;
    sweep.origin = {offset + 32, offset + 1};
    sweep.displacement = {0, -3};
    sweep.dimensions = {.1, 0};
    check(!test.native.shape_cast(sweep, filter), "Excluded compound still blocked a sweep");
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
    for (unsigned motion : {0u, 1u})
        for (bool sensor : {false, true}) {
            bad = test.desired;
            bad.at(1).config.body.motion = motion;
            bad.at(1).config.body.sensor = sensor;
            test.native.validate(bad); // Detached validation never changes the live realization.
        }
    bad = test.desired;
    bad.at(1).config.body.motion = 2;
    reject([&] { test.native.validate(bad); });
    bad = test.desired;
    bad.at(1).config.body.one_way = true;
    test.native.validate(bad); // Static nonsensor painted rectangles share one-way response.
    for (unsigned motion : {1u, 2u}) {
        bad.at(1).config.body.motion = motion;
        reject([&] { test.native.validate(bad); });
    }
    bad.at(1).config.body.motion = 0;
    bad.at(1).config.body.sensor = true;
    reject([&] { test.native.validate(bad); });
    bad = test.desired;
    bad.at(1).config.parts.reset();
    reject([&] { test.native.validate(bad); });
    test.advance();
    check(test.native.state(1, test.desired.at(1).ref)->epoch == before->epoch,
          "Rejected compound candidates prevented valid unchanged continuation");
}
void compound_trigger_budgets() {
    const auto prepare = [](Fixture& test, unsigned count, bool moving, bool visitor_sensor) {
        auto parts = std::make_shared<std::vector<BoxPart2D>>();
        for (unsigned i = 0; i < count; ++i)
            parts->push_back({{float(i % 64) * .02f, float(i / 64) * .02f}, {.01f, .01f}});
        test.desired.at(1).config.parts = std::move(parts);
        test.desired.at(1).config.body.sensor = true;
        test.desired.at(1).config.body.motion = moving ? 1 : 0;
        PhysicsBody2D visitor;
        visitor.motion = 2;
        visitor.sensor = visitor_sensor;
        visitor.gravity_factor = 0;
        test.desired.at(2).config = inline_configuration_2d(visitor, 1, {5, 0}, {1, 1, 1});
    };
    Fixture boundary(1);
    prepare(boundary, 4095, false, false);
    boundary.advance();
    check(boundary.native.shape_size() == 4096 && boundary.native.event_count() == 4095,
          "Maximum compound sensor lost complete native shape-pair events");
    for (std::size_t i = 0; i < boundary.native.event_count(); ++i) {
        const auto event = boundary.native.event(i);
        check(event && event->sensor && event->begin &&
                  event->first == boundary.desired.at(1).ref &&
                  event->second == boundary.desired.at(2).ref,
              "Compound sensor capacity event exposed a cell identity or wrong visitor");
    }
    auto excessive = boundary.desired;
    auto other = excessive.at(2);
    other.ref.entity = EntityId::generate();
    excessive.emplace(3, std::move(other));
    reject([&] { boundary.native.validate(excessive); });
    check(boundary.native.shape_size() == 4096,
          "Rejected sensor shape capacity changed the admitted compound");
    boundary.advance();
    check(!boundary.native.event_count(), "Retained compound overlaps replayed Begin events");

    Fixture overflow(1);
    prepare(overflow, 4094, true, true);
    auto second = overflow.desired.at(2);
    second.ref.entity = EntityId::generate();
    overflow.desired.emplace(3, std::move(second));
    overflow.synchronize();
    check(overflow.native.shape_size() == 4096,
          "Sensor pair-work fixture did not fill the ordinary shape budget");
    reject([&] { overflow.advance(); });
    reject([&] { overflow.native.adopt_events(overflow.tick); });
    reject([&] { overflow.native.checkpoint(overflow.desired, 8 * 1024 * 1024); });
}
void complete_ray_shape_capacity() {
    Fixture test(4095);
    const auto floor = test.desired.at(1).ref;
    auto parts = std::make_shared<std::vector<BoxPart2D>>(*test.desired.at(1).config.parts);
    parts->push_back({{4095.5f, -.5f}, {1, 1}});
    test.desired.at(1).config.parts = parts;
    test.desired.erase(2);
    test.synchronize();
    check(test.native.shape_size() == 4096 && test.native.size() == 1,
          "Maximum XY ray fixture did not admit exactly4096 shapes as one object");
    test.advance(); // Checkpoint only after the normal pose/event adoption boundary.
    const auto before = test.native.checkpoint(test.desired, 4 * 1024 * 1024);
    const std::array<EntityRef, 1> ignored{floor};
    for (unsigned repeat = 0; repeat < 3; ++repeat) {
        const auto all = test.native.raycast_all({-1, -.5}, {4098, 0}, {});
        check(all.size() == 1 && all[0].entity == floor &&
                  std::abs(all[0].fraction - 1. / 4098) < 1e-6,
              "Complete ray failed the admitted XY shape ceiling or duplicated compound parts");
        check(test.native.raycast_all({-1, -.5}, {4098, 0}, {UINT32_MAX, true, ignored}).empty(),
              "Maximum XY shape traversal retained an excluded compound part");
    }
    const auto after = test.native.checkpoint(test.desired, 4 * 1024 * 1024);
    check(before.metadata == after.metadata && before.bytes == after.bytes,
          "Maximum XY complete ray changed solver/checkpoint state");
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
        tile_polygon_projection_and_recovery();
        tile_polygon_controller();
        std::cout
            << "Tile polygon projection/native queries/contacts/sensors/fresh recovery PASS\n";
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
        forge::test::test_one_way_profile();
        forge::test::test_one_way_contact_stability();
        forge::test::test_one_way_rigid();
        forge::test::test_one_way_point_motion();
        forge::test::test_one_way_character();
        forge::test::test_one_way_recovery();
        forge::test::test_one_way_character_recovery();
        forge::test::test_one_way_toggle();
        forge::test::test_one_way_rigid(true);
        forge::test::test_one_way_character(true);
        forge::test::test_one_way_recovery(true);
        std::cout << "One-way rigid/CCD/character/geometric-query/fresh-recovery PASS\n";
        forge::test::test_mover_native_motion();
        forge::test::test_mover_placement_budget();
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
        forge::test::test_platform_carry_motion();
        forge::test::test_platform_carry_rotation();
        forge::test::test_platform_carry_obstruction();
        forge::test::test_platform_carry_lifetime_recovery();
        forge::test::test_platform_carry_dynamic_and_teleport();
        forge::test::test_mover_stance_clearance();
        forge::test::test_mover_stance_filters_and_budget();
        forge::test::test_mover_stance_candidates_and_recovery();
        forge::test::test_mover_stance_sensor_events();
        forge::test::test_mover_stance_one_way_clearance();
        forge::test::test_mover_steps();
        forge::test::test_mover_step_rejections();
        forge::test::test_mover_step_effective_geometry_and_filters();
        forge::test::test_mover_step_recovery();
        forge::test::test_mover_step_carry_and_work_bound();
        forge::test::test_mover_floor_stop();
        forge::test::test_mover_ceiling_response();
        forge::test::test_mover_surface_response_exclusions();
        forge::test::test_mover_surface_response_recovery();
        forge::test::test_mover_velocity_precision();
        std::cout << "Native mover support/carry/floor-follow/lifetime/recovery PASS\n";
        for (double offset : {0., 999000., -999000.})
            geometry_queries_and_recovery(offset);
        capacity_and_invalid_geometry();
        compound_trigger_budgets();
        complete_ray_shape_capacity();
        sensor_retirement();
        connected_collision_and_recovery();
        std::cout << "Compound2D geometry/query/retirement/recovery/capacity PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
