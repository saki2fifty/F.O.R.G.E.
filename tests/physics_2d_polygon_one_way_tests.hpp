#pragma once
#include "physics_debug.hpp"
#include "tile_map_cook.hpp"
#include "tile_set_cook.hpp"
#include <bit>

namespace forge::test {
inline void test_custom_one_way_boundaries() {
    using namespace physics_detail;
    auto source = TileSetSource::create(AssetId::generate());
    source.document["next_id"] = 3;
    source.document["images"] = {{{"id", 1},
                                  {"name", "Boundary image"},
                                  {"material", AssetId::generate()},
                                  {"grid",
                                   {{"columns", 1},
                                    {"rows", 1},
                                    {"margin_x", 0},
                                    {"margin_y", 0},
                                    {"spacing_x", 0},
                                    {"spacing_y", 0}}}}};
    source.document["tiles"] = {
        {{"id", 2}, {"name", "Ring and island"}, {"image", 1}, {"frame", 0}}};
    source.set_collision(
        2, {{"kind", "contours"},
            {"contours",
             Json::array({{{"points", {{0, 0}, {1, 0}, {1, 1}, {0, 1}}},
                           {"holes", Json::array({{{.2, .2}, {.8, .2}, {.8, .8}, {.2, .8}}})}},
                          {{"points", {{.4, .4}, {.6, .4}, {.6, .6}, {.4, .6}}},
                           {"holes", Json::array()}}})}});
    const auto definitions = decode_cooked_tile_set(cook_tile_set(source), source.asset());
    for (unsigned flags = 0; flags < 8; ++flags)
        for (float xs : {-1.f, 1.f})
            for (float ys : {-1.f, 1.f})
                for (unsigned motion : {0u, 1u}) {
                    auto value = TileMapValue::create(AssetId::generate(), source.asset());
                    auto metadata = value.metadata();
                    metadata["cell_size"] = {4, 4};
                    value = value.with_metadata(metadata);
                    const TileMapCellEdit edit{1, {0, 0}, {2, flags}};
                    value = value.paint(std::span(&edit, 1));
                    const auto map = decode_cooked_tile_map(cook_tile_map(value), value.asset());
                    PhysicsBody2D platform;
                    platform.one_way = true;
                    platform.motion = motion;
                    const LocalScale scale{xs, ys, 1};
                    const auto config =
                        tile_map_configuration_2d(platform, map, 1, scale, &definitions);
                    unsigned edges = 0;
                    for (const auto& part : *config.polygons)
                        edges += std::popcount(part.boundary_edges);
                    check_mover(edges == 12,
                                "Contour decomposition changed the twelve original boundary edges");
                    TileMapRenderer renderer;
                    renderer.map = {map.asset()};
                    const Json components{
                        {"forge.physics_body_2d", detail::encode_builtin_value(platform)},
                        {"forge.tile_map_renderer", detail::encode_builtin_value(renderer)},
                        {"forge.tile_map_collider_2d", {{"layer", 1}}}};
                    const auto guide = prepare_physics_debug(components, scale, {}, false, 100,
                                                             &map, &definitions);
                    check_mover(guide.lines.size() == 39 && !guide.truncated,
                                "Custom one-way debug arrows did not identify exactly three "
                                "original front edges");
                    const auto clipped =
                        prepare_physics_debug(components, scale, {}, false, 38, &map, &definitions);
                    check_mover(clipped.lines.size() == 38 && clipped.truncated,
                                "Custom one-way boundary arrows ignored the debug line budget");
                    // Distinct native triangles must not invent a roof across
                    // the void. Probe outer roof, hole floor and island roof.
                    for (unsigned scenario = 0; scenario < 4; ++scenario) {
                        MoverNativeFixture test;
                        const auto floor = test.add(config, 0, 0);
                        test.desired.at(test.id(floor)).pose.scale = scale;
                        PhysicsBody2D body;
                        body.motion = 2;
                        body.gravity_factor = 0;
                        body.bullet = scenario == 3;
                        const double x = scenario == 0 || scenario == 3 ? .4
                                         : scenario == 1                ? 1.2
                                                                        : 2;
                        const double y = scenario == 0 || scenario == 3 ? 6 : scenario == 1 ? 2 : 3;
                        const double expected = scenario == 0 || scenario == 3 ? 4.2
                                                : scenario == 1                ? 1
                                                                               : 2.6;
                        const auto actor = test.add(
                            inline_configuration_2d(body, 1, {.2f, 0}, {1, 1, 1}), x * xs, y * ys);
                        test.synchronize();
                        one_way_velocity(test, actor, {0, (scenario == 3 ? -120 : -2) * ys});
                        for (unsigned tick = 0; tick < 100; ++tick)
                            test.advance();
                        const auto state = test.native.state(test.id(actor), actor);
                        if (!state || std::abs(state->position[1] * ys - expected) > .03)
                            throw std::runtime_error(
                                "Custom one-way boundary landing failed: flags=" +
                                std::to_string(flags) + " xs=" + std::to_string(xs) +
                                " ys=" + std::to_string(ys) + " motion=" + std::to_string(motion) +
                                " scenario=" + std::to_string(scenario) +
                                " y=" + std::to_string(state ? state->position[1] * ys : -999));
                        const auto image = test.image();
                        MoverNativeFixture recovered(test.config);
                        auto remapped = test.desired;
                        auto node = remapped.extract(test.id(floor));
                        node.key() = 101;
                        remapped.insert(std::move(node));
                        recovered.desired = remapped;
                        recovered.tick = test.tick;
                        recovered.native.restore(image.metadata, image.bytes, remapped, test.tick);
                        check_mover(recovered.native.state(test.id(actor), actor) == state,
                                    "Custom one-way recovery lost rigid state");
                        for (unsigned tick = 0; tick < 20; ++tick) {
                            test.advance();
                            recovered.advance();
                            check_mover(
                                recovered.native.state(test.id(actor), actor) ==
                                    test.native.state(test.id(actor), actor),
                                "Custom one-way fresh recovery diverged during continuation");
                        }
                    }
                    // Crossing all undersides and decomposition edges upward
                    // must retain the complete requested velocity and path.
                    MoverNativeFixture passage;
                    const auto floor = passage.add(config, 0, 0);
                    passage.desired.at(passage.id(floor)).pose.scale = scale;
                    PhysicsBody2D body;
                    body.motion = 2;
                    body.gravity_factor = 0;
                    const auto actor = passage.add(
                        inline_configuration_2d(body, 1, {.2f, 0}, {1, 1, 1}), 2 * xs, -.5 * ys);
                    passage.synchronize();
                    one_way_velocity(passage, actor, {0, 6 * ys});
                    for (unsigned tick = 0; tick < 60; ++tick)
                        passage.advance();
                    const auto state = passage.native.state(passage.id(actor), actor);
                    check_mover(
                        state && std::abs(state->position[1] * ys - 5.5) < 1e-4 &&
                            std::abs(state->linear_velocity[1] * ys - 6) < 1e-5,
                        "Custom one-way underside or decomposition edge blocked upward passage");
                }
}
inline void test_custom_one_way_slopes() {
    using namespace physics_detail;
    for (float xs : {-1.f, 1.f})
        for (float ys : {-1.f, 1.f})
            for (double origin : {0., -999800., 999800.})
                for (double initial_angle : {-.3, 0., .3})
                    for (unsigned motion : {0u, 1u}) {
                        const double dt = double(1.f / 60);
                        const Double2 local{-xs / std::sqrt(26.), 5 * ys / std::sqrt(26.)};
                        const Double2 normal{std::cos(initial_angle) * local[0] -
                                                 std::sin(initial_angle) * local[1],
                                             std::sin(initial_angle) * local[0] +
                                                 std::cos(initial_angle) * local[1]};
                        auto settings = MoverGeometryFixture::settings();
                        settings.gravity = {-9.81 * normal[0], -9.81 * normal[1], 0};
                        MoverNativeFixture solid(settings), one_way(settings);
                        const auto setup = [&](MoverNativeFixture& test, bool selective) {
                            Body2DConfiguration floor;
                            floor.shape = 3;
                            floor.body.motion = motion;
                            floor.body.one_way = selective;
                            floor.body.friction = 0;
                            floor.polygons = std::make_shared<const std::vector<PolygonPart2D>>(
                                std::vector<PolygonPart2D>{
                                    {{{{-5 * xs, -ys}, {5 * xs, -ys}, {5 * xs, ys}}}, 3}});
                            const auto ground = test.add(floor, origin, origin,
                                                         {0, 0, float(std::sin(initial_angle / 2)),
                                                          float(std::cos(initial_angle / 2))});
                            test.desired.at(test.id(ground)).pose.scale = {xs, ys, 1};
                            PhysicsBody2D body;
                            body.motion = 2;
                            body.friction = 0;
                            body.allow_sleeping = false;
                            const auto actor =
                                test.add(inline_configuration_2d(body, 1, {.25f, 0}, {1, 1, 1}),
                                         origin + 2 * normal[0], origin + 2 * normal[1]);
                            test.synchronize();
                            return std::pair{ground, actor};
                        };
                        const auto [solid_ground, solid_actor] = setup(solid, false);
                        const auto [ground, actor] = setup(one_way, true);
                        for (unsigned tick = 1; tick <= 120; ++tick) {
                            const auto advance = [&](MoverNativeFixture& test, EntityRef floor) {
                                if (motion) {
                                    auto& pose = test.desired.at(test.id(floor)).pose;
                                    pose.translation.x = origin + .5 * tick * dt;
                                    pose.translation.y = origin + .3 * tick * dt;
                                    const double angle = initial_angle + .15 * tick * dt;
                                    pose.rotation = {0, 0, float(std::sin(angle / 2)),
                                                     float(std::cos(angle / 2))};
                                }
                                test.advance();
                            };
                            advance(solid, solid_ground);
                            advance(one_way, ground);
                            const auto a = solid.native.state(solid.id(solid_actor), solid_actor);
                            const auto b = one_way.native.state(one_way.id(actor), actor);
                            check_mover(
                                a && b &&
                                    std::hypot(a->position[0] - b->position[0],
                                               a->position[1] - b->position[1]) < 1e-5 &&
                                    std::hypot(a->linear_velocity[0] - b->linear_velocity[0],
                                               a->linear_velocity[1] - b->linear_velocity[1]) <
                                        1e-5,
                                "Front-side custom slope response differs from the same ordinary "
                                "moving solid");
                        }
                    }
}
} // namespace forge::test
