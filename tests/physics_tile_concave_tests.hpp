#pragma once
#include "builtins.hpp"
#include "runtime_tile_map.hpp"
#include "tile_asset_fixture.hpp"
#include "tile_asset_worker.hpp"
#include "tile_map_document.hpp"
#include "tile_set_document.hpp"
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <forge/runtime_world.hpp>
#include <iostream>
#include <memory>
#include <optional>
#include <thread>
#include <utility>

inline void test_concave_tile_runtime(const std::filesystem::path& root,
                                      std::shared_ptr<forge::ProjectLease> lease,
                                      const std::filesystem::path& worker,
                                      forge::AssetId image_material) {
    using namespace forge;
    const auto check = [](bool condition, const char* why) {
        if (!condition)
            throw std::runtime_error(why);
    };
    auto definitions = TileSetDocument::create(lease, "Assets/Concave.tileset.json");
    definitions->edit_source(
        definitions->revision(), "Concave corner and empty cell", [&](auto& s) {
            s.document["next_id"] = 4;
            s.document["images"] = {{{"id", 1},
                                     {"name", "Image"},
                                     {"material", image_material},
                                     {"grid",
                                      {{"columns", 1},
                                       {"rows", 1},
                                       {"margin_x", 0},
                                       {"margin_y", 0},
                                       {"spacing_x", 0},
                                       {"spacing_y", 0}}}}};
            s.document["tiles"] = {
                {{"id", 2}, {"name", "Concave corner"}, {"image", 1}, {"frame", 0}},
                {{"id", 3}, {"name", "Empty collision"}, {"image", 1}, {"frame", 0}}};
            s.set_collision(
                2, {{"kind", "polygon"},
                    {"points", {{0., 0.}, {1., 0.}, {1., .5}, {.5, .5}, {.5, 1.}, {0., 1.}}}});
            s.set_collision(3, {{"kind", "none"}});
        });
    definitions->save();
    auto map = TileMapDocument::create(lease, "Assets/Concave.tilemap.json", [&](AssetId id) {
        return TileMapSource{TileMapValue::create(id, definitions->source().asset())};
    });
    map->edit_source(map->revision(), "Four-metre concave tile", [](auto& s) {
        auto metadata = s.document.metadata();
        metadata["cell_size"] = {4., 4.};
        s.document = s.document.with_metadata(std::move(metadata));
        const std::array<TileMapCellEdit, 1> edits{{{1, {0, 0}, {2, 0}}}};
        s.document = s.document.paint(edits);
    });
    map->save();
    AssetImportService importer(lease, tile_asset_import_registry(worker),
                                {"linux", "none", "cpu"});
    for (const bool is_map : {false, true}) {
        const auto result = test_tile_asset_io::publish(
            importer, is_map ? map->locator() : definitions->locator(),
            is_map ? map->source().asset() : definitions->source().asset(), is_map);
        if (!result.published)
            throw std::runtime_error("Concave publication failed: " + result.diagnostic);
    }
    const auto catalog = AssetCatalog::open_project(root);
    const auto& record = catalog.records().at(definitions->source().asset());
    check(record.schema_version == 3 &&
              record.metadata.at("forge.import").at("output_version") == tile_set_cooked_version,
          "Concave source/cook publication versions differ");
    std::cout << "Concave source3/current cooked publication PASS\n";
    const auto scene = AssetId::generate();
    const auto floor = EntityId::generate();
    const auto upper = EntityId::generate(), lower = EntityId::generate();
    PhysicsBody2D dynamic;
    dynamic.motion = 2;
    TileMapRenderer renderer;
    renderer.map = {map->source().asset()};
    TileMapCollider2D collider;
    collider.layer = 1;
    CircleCollider2D circle;
    circle.radius = .1f;
    Json document{{"version", 5}, {"asset_id", scene}, {"entities", Json::array()}};
    document["entities"].push_back(
        {{"id", floor},
         {"name", "Concave floor"},
         {"components",
          {{"forge.local_translation", {{"x", 0}, {"y", 0}, {"z", 0}}},
           {"forge.tile_map_renderer", detail::encode_builtin_value(renderer)},
           {"forge.tile_map_collider_2d", detail::encode_builtin_value(collider)},
           {"forge.physics_body_2d", detail::encode_builtin_value(PhysicsBody2D{})}}}});
    for (const auto& [id, x] : std::array{std::pair{upper, 1.}, std::pair{lower, 3.}})
        document["entities"].push_back(
            {{"id", id},
             {"name", "Falling circle"},
             {"spatial", {{"mode", "world"}}},
             {"components",
              {{"forge.local_translation", {{"x", x}, {"y", 6}, {"z", 0}}},
               {"forge.physics_body_2d", detail::encode_builtin_value(dynamic)},
               {"forge.circle_collider_2d", detail::encode_builtin_value(circle)}}}});
    PhysicsConfig physics;
    physics.gravity = {0, -9.81, 0};
    RuntimeWorld world({}, physics, std::nullopt, root, false);
    world.scene.restore_snapshot(document);
    const auto prepare = [&](RuntimeWorld& value) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (!value.physics()->prepare_assets() || !value.prepare_scene_resources()) {
            check(std::chrono::steady_clock::now() < end,
                  "Concave RuntimeWorld preparation stalled");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };
    prepare(world);
    std::cout << "Concave RuntimeWorld resource/native preparation PASS\n";
    const EntityRef floor_ref{scene, floor}, upper_ref{scene, upper}, lower_ref{scene, lower};
    const auto ray = world.physics()->raycast_2d({3, 5}, {0, -6});
    check(ray && ray->entity == floor_ref && std::abs(ray->fraction - .5) < .001,
          "Actual cooked concave floor filled the empty notch");
    auto& context = world.engine.world();
    const auto original = detail::runtime_tile_map_snapshot(context, floor_ref);
    const auto before = world.physics()->body_state_2d(floor_ref);
    check(bool(before) && world.physics()->status().at("shapes_2d") == 6,
          "Concave RuntimeWorld lost its existing single body/four convex pieces");
    const std::array<TileMapCellEdit, 1> invalid{{{1, {500000, 0}, {2, 0}}}};
    const auto bad = detail::request_runtime_tile_cells(context, "project.concave", floor_ref,
                                                        original->generation, invalid);
    world.simulation.tick(1.f / 60);
    check(detail::inspect_runtime_tile_edit(context, "project.concave", bad).state ==
                  detail::RuntimeTileEditState::Failed &&
              detail::runtime_tile_map_snapshot(context, floor_ref) == original &&
              world.physics()->body_state_2d(floor_ref)->epoch == before->epoch,
          "Rejected concave extent changed admitted cells/native body");
    detail::release_runtime_tile_edit(context, "project.concave", bad);
    const auto edit = [&](std::uint64_t tile) {
        const auto value = detail::runtime_tile_map_snapshot(context, floor_ref);
        const std::array<TileMapCellEdit, 1> cells{{{1, {0, 0}, {tile, 0}}}};
        const auto request = detail::request_runtime_tile_cells(
            context, "project.concave", floor_ref, value->generation, cells);
        world.simulation.tick(1.f / 60);
        check(detail::inspect_runtime_tile_edit(context, "project.concave", request).state ==
                  detail::RuntimeTileEditState::Applied,
              "Concave empty/refill edit failed");
        detail::release_runtime_tile_edit(context, "project.concave", request);
    };
    edit(3);
    check(!world.physics()->body_state_2d(floor_ref), "None retained decomposed native collision");
    edit(2);
    check(world.physics()->body_state_2d(floor_ref)->epoch != before->epoch,
          "Concave refill inherited a retired native epoch");
    for (unsigned tick = 0; tick < 180; ++tick)
        world.simulation.tick(1.f / 60);
    check(std::abs(world.physics()->body_state_2d(upper_ref)->position[1] - 4.1) < .03 &&
              std::abs(world.physics()->body_state_2d(lower_ref)->position[1] - 2.1) < .03,
          "Actual source/cook/runtime concave landing lost notch or upper lane");
    const auto physical = world.physics()->checkpoint();
    const auto maps = detail::checkpoint_runtime_tile_maps(context);
    RuntimeWorld recovered({}, physics, std::nullopt, root, false);
    recovered.scene.restore_snapshot(world.scene.snapshot());
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    for (;;) {
        recovered.engine.services().resources()->synchronize();
        detail::synchronize_runtime_tile_maps(recovered.engine.world());
        if (detail::prepare_runtime_tile_maps(recovered.engine.world()))
            break;
        check(std::chrono::steady_clock::now() < end, "Concave fresh recovery resources stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    detail::restore_runtime_tile_maps(recovered.engine.world(), maps);
    recovered.physics()->restore(physical);
    recovered.simulation.restore_input_tick(
        world.physics()->status().at("tick").get<std::uint64_t>());
    recovered.simulation.reset_presentation();
    for (unsigned tick = 0; tick < 20; ++tick) {
        world.simulation.tick(1.f / 60);
        recovered.simulation.tick(1.f / 60);
        for (const auto ref : {floor_ref, upper_ref, lower_ref})
            check(world.physics()->body_state_2d(ref) == recovered.physics()->body_state_2d(ref),
                  "Actual cooked concave fresh-world continuation diverged");
    }
    std::cout << "Concave source3/current cooked native queries, landing, rejected edit, "
                 "empty/refill and "
                 "exact RuntimeWorld recovery PASS\n";
}
