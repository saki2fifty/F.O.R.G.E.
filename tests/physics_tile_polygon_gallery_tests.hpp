#pragma once
#include "runtime_tile_map.hpp"
#include <cmath>
#include <forge/game_content.hpp>
#include <forge/runtime_world.hpp>
#include <thread>
#include <vector>

inline void test_tile_polygon_gallery(const std::filesystem::path& root, forge::AssetId scene,
                                      forge::AssetId map) {
    using namespace forge;
    const auto check = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    RuntimeWorld world({}, {}, std::nullopt, root, false);
    world.scene.restore_snapshot(load_game_scene(root, {scene}));
    const auto prepare = [&](RuntimeWorld& target) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (!target.physics()->prepare_assets() || !target.prepare_scene_resources()) {
            check(std::chrono::steady_clock::now() < deadline,
                  "Polygon Gallery preparation stalled");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };
    prepare(world);
    EntityRef floor, box;
    std::vector<std::pair<EntityRef, double>> circles;
    const auto authored = world.scene.document();
    for (const auto& row : authored.at("entities")) {
        const EntityRef ref{scene, row.at("id").get<EntityId>()};
        const auto& components = row.at("components");
        if (components.contains("forge.tile_map_renderer")) {
            check(components.at("forge.tile_map_renderer").at("map").get<AssetId>() == map,
                  "Polygon Gallery Map root changed");
            floor = ref;
        }
        if (components.contains("forge.box_collider_2d"))
            box = ref;
        if (components.contains("forge.circle_collider_2d")) {
            const auto state = world.physics()->body_state_2d(ref);
            check(bool(state), "Gallery circle did not realize its native body");
            circles.emplace_back(ref, state->position[0]);
        }
    }
    check(bool(floor.entity) && bool(box.entity) && world.physics()->status().at("bodies_2d") == 5,
          "Polygon Gallery lost its compound body/four independent actors");
    const auto snapshot = detail::runtime_tile_map_snapshot(world.engine.world(), floor);
    check(snapshot->definition_values.tile(3).collision.kind == TileCollisionKind::Convex &&
              snapshot->definition_values.tile(4).collision.kind == TileCollisionKind::None,
          "Polygon Gallery loaded obsolete full-cell definitions");
    for (double x : {-4.75, 4.75}) {
        const auto ray = world.physics()->raycast_2d({x, 2}, {0, -8});
        check(ray && ray->entity == floor && std::abs(ray->fraction - .59375) < .001,
              "Original/mirrored ramp differs from the visible polygon course");
    }
    check(!world.physics()->raycast_2d({.5, 3}, {0, -2}),
          "Decorative Gallery tile retained collision");
    for (unsigned tick = 0; tick < 180; ++tick)
        world.simulation.tick(1.f / 60);
    const auto state = world.physics()->body_state_2d(box);
    check(state && std::abs(state->position[1] + 2.55) < .03,
          "Gallery box did not pass through decoration and land on the full-cell floor");
    check(circles.size() == 3, "Gallery lost its three ramp actors");
    for (const auto& [circle, x] : circles) {
        const auto current = world.physics()->body_state_2d(circle);
        check(current && std::isfinite(current->position[0]) &&
                  std::abs(current->position[0] - x) > .25,
              "Gallery circle did not roll away from its starting column on the convex course");
    }
    const auto physics = world.physics()->checkpoint();
    const auto maps = detail::checkpoint_runtime_tile_maps(world.engine.world());
    RuntimeWorld copy({}, {}, std::nullopt, root, false);
    copy.scene.restore_snapshot(world.scene.snapshot());
    const auto recovery_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    for (;;) {
        copy.engine.services().resources()->synchronize();
        detail::synchronize_runtime_tile_maps(copy.engine.world());
        if (detail::prepare_runtime_tile_maps(copy.engine.world()))
            break;
        check(std::chrono::steady_clock::now() < recovery_deadline,
              "Unpublished polygon recovery resources did not settle");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    detail::restore_runtime_tile_maps(copy.engine.world(), maps);
    copy.physics()->restore(physics);
    copy.simulation.restore_input_tick(world.physics()->status().at("tick").get<std::uint64_t>());
    copy.simulation.reset_presentation();
    for (unsigned tick = 0; tick < 60; ++tick) {
        world.simulation.tick(1.f / 60);
        copy.simulation.tick(1.f / 60);
        check(world.physics()->body_state_2d(box) == copy.physics()->body_state_2d(box),
              "Cooked polygon Gallery diverged after cold-world recovery");
        for (const auto& [circle, x] : circles) {
            (void)x;
            check(world.physics()->body_state_2d(circle) == copy.physics()->body_state_2d(circle),
                  "Polygon Gallery circle diverged after cold-world recovery");
        }
    }
}
