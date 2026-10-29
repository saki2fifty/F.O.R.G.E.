#pragma once
#include "builtins.hpp"
#include "runtime_tile_map.hpp"
#include "tile_asset_fixture.hpp"
#include "tile_asset_worker.hpp"
#include "tile_map_document.hpp"
#include "tile_set_document.hpp"
#include <cmath>
#include <forge/runtime_world.hpp>
#include <iostream>
#include <thread>

inline void test_tile_polygon_runtime(const std::filesystem::path& root,
                                      std::shared_ptr<forge::ProjectLease> lease,
                                      const std::filesystem::path& worker, forge::AssetId set) {
    using namespace forge;
    using namespace forge::detail;
    using namespace std::chrono_literals;
    const auto check = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    auto map = TileMapDocument::create(lease, "Assets/Profiles.tilemap.json", [&](AssetId id) {
        return TileMapSource{TileMapValue::create(id, set)};
    });
    map->edit_source(map->revision(), "Ramp and decorative cell", [](auto& source) {
        auto metadata = source.document.metadata();
        metadata["cell_size"] = {4., 1.};
        source.document = source.document.with_metadata(std::move(metadata));
        const std::array<TileMapCellEdit, 2> cells{{{1, {0, -1}, {2, 0}}, {1, {1, -1}, {3, 0}}}};
        source.document = source.document.paint(cells);
    });
    map->save();
    AssetImportService importer(lease, tile_asset_import_registry(worker),
                                {"linux", "none", "cpu"});
    const auto first_publication =
        test_tile_asset_io::publish(importer, map->locator(), map->source().asset(), true);
    if (!first_publication.published)
        throw std::runtime_error("Runtime polygon Map publication failed: " +
                                 first_publication.diagnostic);
    TileMapRenderer renderer;
    renderer.map = {map->source().asset()};
    const auto scene = AssetId::generate();
    const auto floor = EntityId::generate(), actor = EntityId::generate();
    PhysicsBody2D dynamic_body;
    dynamic_body.motion = 2;
    std::cout << "Polygon Map source/catalog publication PASS\n";
    Json floor_components{{"forge.local_translation", {{"x", 0}, {"y", 0}, {"z", 0}}},
                          {"forge.tile_map_renderer", detail::encode_builtin_value(renderer)},
                          {"forge.tile_map_collider_2d", {{"layer", 1}}},
                          {"forge.physics_body_2d", detail::encode_builtin_value(PhysicsBody2D{})}};
    Json actor_components{{"forge.local_translation", {{"x", 3}, {"y", 3}, {"z", 0}}},
                          {"forge.physics_body_2d", detail::encode_builtin_value(dynamic_body)},
                          {"forge.circle_collider_2d", {{"radius", .25}}}};
    Json document{{"version", 5},
                  {"asset_id", scene},
                  {"entities",
                   Json::array({{{"id", floor}, {"name", "Ramp"}, {"components", floor_components}},
                                {{"id", actor},
                                 {"name", "Falling actor"},
                                 {"spatial", {{"mode", "world"}}},
                                 {"components", actor_components}}})}};
    RuntimeWorld world({}, {}, std::nullopt, root, false);
    world.scene.restore_snapshot(document);
    const auto prepare = [&](RuntimeWorld& target) {
        const auto deadline = std::chrono::steady_clock::now() + 15s;
        while (!target.physics()->prepare_assets() || !target.prepare_scene_resources()) {
            check(std::chrono::steady_clock::now() < deadline, "Polygon Map preparation stalled");
            std::this_thread::sleep_for(1ms);
        }
    };
    prepare(world);
    std::cout << "Polygon RuntimeWorld resource/native preparation PASS\n";
    const EntityRef floor_ref{scene, floor}, actor_ref{scene, actor};
    auto& context = world.engine.world();
    const auto initial = runtime_tile_map_snapshot(context, floor_ref);
    const auto epoch = world.physics()->body_state_2d(floor_ref)->epoch;
    const auto ray = world.physics()->raycast_2d({3, 2}, {0, -5});
    check(ray && ray->entity == floor_ref && std::abs(ray->fraction - .45) < .001 &&
              !world.physics()->raycast_2d({6, 2}, {0, -5}),
          "Runtime matching definitions lost ramp geometry or retained decorative collision");
    const std::array<TileMapCellEdit, 1> invalid{{{1, {50000, -1}, {2, 0}}}};
    const auto request = request_runtime_tile_cells(context, "project.polygons", floor_ref,
                                                    initial->generation, invalid);
    world.simulation.tick(1.f / 60);
    check(inspect_runtime_tile_edit(context, "project.polygons", request).state ==
                  RuntimeTileEditState::Failed &&
              runtime_tile_map_snapshot(context, floor_ref) == initial &&
              world.physics()->body_state_2d(floor_ref)->epoch == epoch,
          "Invalid queued polygon extent changed the adopted Map/body");
    release_runtime_tile_edit(context, "project.polygons", request);
    std::cout << "Polygon native queries/queued rejection PASS\n";
    const auto physics = world.physics()->checkpoint();
    const auto maps = checkpoint_runtime_tile_maps(context);
    RuntimeWorld recovered({}, {}, std::nullopt, root, false);
    recovered.scene.restore_snapshot(world.scene.snapshot());
    const auto recovery_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    for (;;) {
        recovered.engine.services().resources()->synchronize();
        detail::synchronize_runtime_tile_maps(recovered.engine.world());
        if (detail::prepare_runtime_tile_maps(recovered.engine.world()))
            break;
        check(std::chrono::steady_clock::now() < recovery_deadline,
              "Unpublished polygon recovery resources did not settle");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    restore_runtime_tile_maps(recovered.engine.world(), maps);
    recovered.physics()->restore(physics);
    recovered.simulation.restore_input_tick(
        world.physics()->status().at("tick").get<std::uint64_t>());
    recovered.simulation.reset_presentation();
    for (unsigned tick = 0; tick < 20; ++tick) {
        world.simulation.tick(1.f / 60);
        recovered.simulation.tick(1.f / 60);
        check(world.physics()->body_state_2d(actor_ref) ==
                  recovered.physics()->body_state_2d(actor_ref),
              "Fresh RuntimeWorld polygon continuation diverged");
    }
    std::cout << "Polygon fresh RuntimeWorld continuation PASS\n";
    const auto actor_epoch = world.physics()->body_state_2d(actor_ref)->epoch;
    const std::array<TileMapCellEdit, 1> empty{{{1, {0, -1}, {3, 0}}}};
    const auto clear = request_runtime_tile_cells(context, "project.polygons", floor_ref,
                                                  initial->generation, empty);
    world.simulation.tick(1.f / 60);
    check(inspect_runtime_tile_edit(context, "project.polygons", clear).state ==
                  RuntimeTileEditState::Applied &&
              !world.physics()->body_state_2d(floor_ref) &&
              world.physics()->body_state_2d(actor_ref)->epoch == actor_epoch,
          "All-None layer retained a collider or reset an unrelated body");
    release_runtime_tile_edit(context, "project.polygons", clear);
    const std::array<TileMapCellEdit, 1> filled{{{1, {0, -1}, {2, 1}}}};
    const auto refill = request_runtime_tile_cells(context, "project.polygons", floor_ref,
                                                   initial->generation, filled);
    world.simulation.tick(1.f / 60);
    check(inspect_runtime_tile_edit(context, "project.polygons", refill).state ==
                  RuntimeTileEditState::Applied &&
              world.physics()->body_state_2d(floor_ref)->epoch != epoch &&
              world.physics()->body_state_2d(actor_ref)->epoch == actor_epoch,
          "Flipped polygon refill lost retirement or unrelated native state");
    release_runtime_tile_edit(context, "project.polygons", refill);
    const auto reset =
        request_runtime_tile_reset(context, "project.polygons", floor_ref, initial->generation);
    world.simulation.tick(1.f / 60);
    check(inspect_runtime_tile_edit(context, "project.polygons", reset).state ==
              RuntimeTileEditState::Applied,
          "Polygon Map reset failed");
    release_runtime_tile_edit(context, "project.polygons", reset);
    const auto before_replacement = runtime_tile_map_snapshot(context, floor_ref);
    const auto replacement_epoch = world.physics()->body_state_2d(floor_ref)->epoch;
    TileSetDocument definitions(lease, "Assets/Profiles.tileset.json");
    definitions.edit_source(
        definitions.revision(), "Too small at physical scale", [](auto& source) {
            source.set_collision(
                2, {{"kind", "convex"}, {"points", {{0., 0.}, {.00001, 0.}, {.00001, .00001}}}});
        });
    definitions.save();
    const auto set_replacement =
        test_tile_asset_io::publish(importer, definitions.locator(), set, false);
    if (!set_replacement.published)
        throw std::runtime_error("Replacement polygon Set publication failed: " +
                                 set_replacement.diagnostic);
    const auto map_replacement =
        test_tile_asset_io::publish(importer, map->locator(), map->source().asset(), true);
    if (!map_replacement.published)
        throw std::runtime_error("Replacement polygon Map publication failed: " +
                                 map_replacement.diagnostic);
    world.engine.services().resources()->refresh();
    const auto deadline = std::chrono::steady_clock::now() + 15s;
    for (;;) {
        (void)world.physics()->prepare_assets();
        (void)world.prepare_scene_resources();
        const auto status = inspect_runtime_tile_map(context, floor_ref);
        if (status.ready && !status.current && !status.pending && !status.diagnostic.empty())
            break;
        check(std::chrono::steady_clock::now() < deadline,
              "Invalid polygon replacement did not settle");
        std::this_thread::sleep_for(1ms);
    }
    world.simulation.tick(1.f / 60);
    check(runtime_tile_map_snapshot(context, floor_ref) == before_replacement &&
              world.physics()->body_state_2d(floor_ref)->epoch == replacement_epoch &&
              world.physics()->body_state_2d(actor_ref)->epoch == actor_epoch,
          "Physically invalid definition replacement retired a usable Map or unrelated body");
}
