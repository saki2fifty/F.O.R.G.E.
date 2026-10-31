#pragma once
#include "builtins.hpp"
#include "runtime_tile_map.hpp"
#include "tile_animation.hpp"
#include "tile_asset_fixture.hpp"
#include "tile_map_document.hpp"
#include "tile_set_document.hpp"
#include <array>
#include <chrono>
#include <forge/render_scene.hpp>
#include <forge/runtime_world.hpp>
#include <iostream>
#include <limits>
#include <thread>

inline void test_animated_tile_runtime(const std::filesystem::path& root,
                                       std::shared_ptr<forge::ProjectLease> lease,
                                       const std::filesystem::path& worker,
                                       forge::AssetId material) {
    using namespace forge;
    using test_tile_asset_io::check;
    auto definitions = TileSetDocument::create(lease, "Assets/Animated.tileset.json");
    definitions->edit_source(definitions->revision(), "Looping tile sequence", [&](auto& source) {
        source.document["next_id"] = 3;
        source.document["images"] = {{{"id", 1},
                                      {"name", "Four-frame image"},
                                      {"material", material},
                                      {"grid",
                                       {{"columns", 4},
                                        {"rows", 1},
                                        {"margin_x", 0},
                                        {"margin_y", 0},
                                        {"spacing_x", 0},
                                        {"spacing_y", 0}}}}};
        source.document["tiles"] = {{{"id", 2}, {"name", "Loop"}, {"image", 1}, {"frame", 2}}};
        source.set_animation(2, {{"enabled", true},
                                 {"frames",
                                  {{{"frame", 0}, {"seconds", .125}},
                                   {{"frame", 3}, {"seconds", .25}},
                                   {{"frame", 1}, {"seconds", .125}}}}});
    });
    definitions->save();
    auto map = TileMapDocument::create(lease, "Assets/Animated.tilemap.json", [&](AssetId id) {
        return TileMapSource{TileMapValue::create(id, definitions->source().asset())};
    });
    map->edit_source(map->revision(), "One animated solid cell", [](auto& source) {
        const std::array<TileMapCellEdit, 1> cells{{{1, {0, 0}, {2, 0}}}};
        source.document = source.document.paint(cells);
    });
    map->save();
    AssetImportService importer(lease, tile_asset_import_registry(worker),
                                {"linux", "none", "cpu"});
    for (bool is_map : {false, true}) {
        const auto outcome = test_tile_asset_io::publish(
            importer, is_map ? map->locator() : definitions->locator(),
            is_map ? map->source().asset() : definitions->source().asset(), is_map);
        check(outcome.published, "Animated tile publication failed: " + outcome.diagnostic);
    }
    const auto catalog = AssetCatalog::open_project(root);
    const auto& record = catalog.records().at(definitions->source().asset());
    check(record.schema_version == 4 &&
              record.metadata.at("forge.import").at("output_version") == 4,
          "Animated source/cook publication versions differ");
    const auto scene = AssetId::generate();
    const auto floor = EntityId::generate();
    TileMapRenderer renderer;
    renderer.map = {map->source().asset()};
    TileMapCollider2D collider;
    collider.layer = 1;
    Json document{
        {"version", 5},
        {"asset_id", scene},
        {"entities",
         {{{"id", floor},
           {"name", "Animated solid floor"},
           {"components",
            {{"forge.local_translation", {{"x", 0}, {"y", 0}, {"z", 0}}},
             {"forge.tile_map_renderer", detail::encode_builtin_value(renderer)},
             {"forge.tile_map_collider_2d", detail::encode_builtin_value(collider)},
             {"forge.physics_body_2d", detail::encode_builtin_value(PhysicsBody2D{})}}}}}}};
    const auto prepare = [&](RuntimeWorld& world) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (!world.physics()->prepare_assets() || !world.prepare_scene_resources()) {
            check(std::chrono::steady_clock::now() < deadline,
                  "Animated runtime resources stalled");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };
    const auto observed = [](RuntimeWorld& world) {
        auto frame = extract_render_scene(world.simulation.presentation(1));
        detail::bind_runtime_tile_maps(world.engine.world(), frame);
        check(frame.tile_maps.size() == 1 && frame.tile_maps[0].cells &&
                  frame.tile_maps[0].definitions,
              "Runtime animation lost its complete Map view");
        return frame.tile_maps[0];
    };
    RuntimeWorld world({}, {}, std::nullopt, root, false);
    world.scene.restore_snapshot(document);
    prepare(world);
    const auto ref = world.scene.reference(floor.str());
    const auto first = observed(world);
    const auto initial_body = world.physics()->body_state_2d(ref);
    check(initial_body && first.animation_seconds == 0,
          "New world animation did not start at zero");
    for (unsigned tick = 1; tick <= 6; ++tick) {
        world.simulation.tick(.0625f);
        const auto frame = observed(world);
        const std::array<unsigned, 6> expected{0, 3, 3, 3, 3, 1};
        check(frame.animation_seconds == tick / 16. &&
                  tile_detail::tile_frame_at(frame.definitions->tile(2), frame.animation_seconds) ==
                      expected[tick - 1],
              "Actual completed fixed ticks selected a different frame");
        check(frame.cells == first.cells && frame.definitions == first.definitions &&
                  frame.generation == first.generation && frame.revision == first.revision &&
                  world.physics()->body_state_2d(ref)->epoch == initial_body->epoch,
              "Animation changed Map values, resource generation or native collision epoch");
    }
    const auto physical = world.physics()->checkpoint();
    const auto maps = detail::checkpoint_runtime_tile_maps(world.engine.world());
    check(maps.at("version") == 2 && maps.at("animation_seconds") == .375,
          "Map checkpoint lost animation time");
    RuntimeWorld recovered({}, {}, std::nullopt, root, false);
    recovered.scene.restore_snapshot(world.scene.snapshot());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    for (;;) {
        recovered.engine.services().resources()->synchronize();
        detail::synchronize_runtime_tile_maps(recovered.engine.world());
        if (detail::prepare_runtime_tile_maps(recovered.engine.world()))
            break;
        check(std::chrono::steady_clock::now() < deadline, "Fresh animation resources stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto before = detail::checkpoint_runtime_tile_maps(recovered.engine.world());
    for (unsigned scenario = 0; scenario < 4; ++scenario) {
        auto bad = maps;
        if (scenario == 0) {
            bad["version"] = 1;
            bad.erase("animation_seconds");
        }
        if (scenario == 1)
            bad["animation_seconds"] = -1;
        if (scenario == 2)
            bad["animation_seconds"] = true;
        if (scenario == 3)
            bad["animation_seconds"] = std::numeric_limits<double>::quiet_NaN();
        bool rejected = false;
        try {
            detail::restore_runtime_tile_maps(recovered.engine.world(), bad);
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected && detail::checkpoint_runtime_tile_maps(recovered.engine.world()) == before,
              "Invalid animation checkpoint partially changed the fresh world");
    }
    detail::restore_runtime_tile_maps(recovered.engine.world(), maps);
    recovered.physics()->restore(physical);
    recovered.simulation.restore_input_tick(
        world.physics()->status().at("tick").get<std::uint64_t>());
    recovered.simulation.reset_presentation();
    check(observed(recovered).animation_seconds == .375, "Recovery reset the animation phase");
    for (unsigned tick = 0; tick < 20; ++tick) {
        world.simulation.tick(.0625f);
        recovered.simulation.tick(.0625f);
        check(observed(world).animation_seconds == observed(recovered).animation_seconds &&
                  world.physics()->body_state_2d(ref) == recovered.physics()->body_state_2d(ref),
              "Recovered animation/native continuation diverged");
    }
    std::cout << "Actual source4/cooked4 fixed tile animation, immutable reuse, static collision "
                 "and exact recovery PASS\n";
}
