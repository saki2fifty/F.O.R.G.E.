#pragma once
#include "builtins.hpp"
#include "runtime_tile_map.hpp"
#include "tile_animation.hpp"
#include "tile_asset_fixture.hpp"
#include "tile_map_collision.hpp"
#include "tile_map_document.hpp"
#include "tile_set_document.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <forge/render_scene.hpp>
#include <forge/runtime_world.hpp>
#include <iostream>
#include <limits>
#include <thread>

inline void test_animated_tile_runtime(const std::filesystem::path& root,
                                       std::shared_ptr<forge::ProjectLease> lease,
                                       const std::filesystem::path& worker, forge::AssetId material,
                                       bool neighbors = false) {
    using namespace forge;
    using test_tile_asset_io::check;
    auto definitions = TileSetDocument::create(
        lease, neighbors ? "Assets/NeighborAnimated.tileset.json" : "Assets/Animated.tileset.json");
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
        if (neighbors) {
            source.document["next_id"] = 5;
            source.document["tiles"].push_back(
                {{"id", 3}, {"name", "Neighbor root"}, {"image", 1}, {"frame", 0}});
            source.document["tiles"].push_back(
                {{"id", 4}, {"name", "Edge shape"}, {"image", 1}, {"frame", 1}});
            source.set_collision(4,
                                 {{"kind", "convex"}, {"points", {{0., 0.}, {1., 0.}, {.5, 1.}}}});
            source.set_autotile(3, {{"enabled", true},
                                    {"rules",
                                     {{{"mask", 1}, {"same", 1}, {"tile", 2}},
                                      {{"mask", 1}, {"same", 0}, {"tile", 4}}}}});
        }
    });
    definitions->save();
    auto map = TileMapDocument::create(
        lease, neighbors ? "Assets/NeighborAnimated.tilemap.json" : "Assets/Animated.tilemap.json",
        [&](AssetId id) {
            return TileMapSource{TileMapValue::create(id, definitions->source().asset())};
        });
    map->edit_source(map->revision(), "Animated solid cells", [&](auto& source) {
        const std::vector<TileMapCellEdit> cells =
            neighbors ? std::vector<TileMapCellEdit>{{1, {0, 0}, {3, 0}}, {1, {0, 1}, {3, 0}}}
                      : std::vector<TileMapCellEdit>{{1, {0, 0}, {2, 0}}};
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
    check(record.schema_version == (neighbors ? 5 : 4) &&
              record.metadata.at("forge.import").at("output_version") == tile_set_cooked_version,
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
    const auto other = EntityId::generate();
    if (neighbors)
        document["entities"].push_back(
            {{"id", other},
             {"name", "Independent shared Map instance"},
             {"components",
              {{"forge.local_translation", {{"x", 100}, {"y", 0}, {"z", 0}}},
               {"forge.tile_map_renderer", detail::encode_builtin_value(renderer)}}}});
    const auto prepare = [&](RuntimeWorld& world) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (!world.physics()->prepare_assets() || !world.prepare_scene_resources()) {
            check(std::chrono::steady_clock::now() < deadline,
                  "Animated runtime resources stalled");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };
    const auto observed = [&](RuntimeWorld& world) {
        auto frame = extract_render_scene(world.simulation.presentation(1));
        detail::bind_runtime_tile_maps(world.engine.world(), frame);
        const auto selected =
            std::find_if(frame.tile_maps.begin(), frame.tile_maps.end(),
                         [&](const auto& value) { return value.entity == floor; });
        check(frame.tile_maps.size() == (neighbors ? 2 : 1) && selected != frame.tile_maps.end() &&
                  selected->cells && selected->definitions,
              "Runtime animation lost its complete Map view");
        return *selected;
    };
    RuntimeWorld world({}, {}, std::nullopt, root, false);
    world.scene.restore_snapshot(document);
    prepare(world);
    const auto ref = world.scene.reference(floor.str());
    const auto first = observed(world);
    const auto initial_body = world.physics()->body_state_2d(ref);
    check(initial_body && first.animation_seconds == 0,
          "New world animation did not start at zero");
    if (neighbors) {
        const auto alias = detail::runtime_tile_map_snapshot(world.engine.world(), {scene, other});
        check(first.variants && alias->variants && alias->generation != first.generation &&
                  alias->variants->overrides({1, {0, 0}}).data() ==
                      first.variants->overrides({1, {0, 0}}).data(),
              "Independent actor adoption duplicated immutable neighbor override storage");
        check(first.variants &&
                  first.variants->matches(first.cells.get(), first.definitions.get()) &&
                  first.cells->cell(1, {}).tile == 3 &&
                  first.variants->overrides({1, {0, 0}}).size() == 2,
              "Actual runtime lost selected variants or overwrote logical cell IDs");
        const auto geometry = tile_map_collision_geometry(*first.cells, *first.definitions, 1, 4096,
                                                          first.variants.get());
        check(geometry.rectangles.size() == 1 && geometry.polygons.size() == 1,
              "Actual runtime did not adopt mixed selected collision profiles");
    }
    for (unsigned tick = 1; tick <= 6; ++tick) {
        world.simulation.tick(.0625f);
        const auto frame = observed(world);
        const std::array<unsigned, 6> expected{0, 3, 3, 3, 3, 1};
        check(frame.animation_seconds == tick / 16. &&
                  tile_detail::tile_frame_at(frame.definitions->tile(2), frame.animation_seconds) ==
                      expected[tick - 1],
              "Actual completed fixed ticks selected a different frame");
        check(frame.cells == first.cells && frame.definitions == first.definitions &&
                  frame.variants == first.variants && frame.generation == first.generation &&
                  frame.revision == first.revision &&
                  world.physics()->body_state_2d(ref)->epoch == initial_body->epoch,
              "Animation changed Map values, resource generation or native collision epoch");
    }
    if (neighbors) {
        const std::array<TileMapCellEdit, 1> erase{{{1, {0, 1}, {}}}};
        const auto token = detail::request_runtime_tile_cells(
            world.engine.world(), "project.neighbor_test", ref, first.generation, erase);
        world.simulation.tick(.0625f);
        const auto changed = observed(world);
        const auto geometry = tile_map_collision_geometry(*changed.cells, *changed.definitions, 1,
                                                          4096, changed.variants.get());
        check(
            detail::inspect_runtime_tile_edit(world.engine.world(), "project.neighbor_test", token)
                        .state == detail::RuntimeTileEditState::Applied &&
                changed.revision == 1 && changed.cells->cell(1, {}).tile == 3 &&
                geometry.rectangles.empty() && geometry.polygons.size() == 1 &&
                world.physics()->body_state_2d(ref)->epoch != initial_body->epoch &&
                first.cells->cell(1, {0, 1}).tile == 3,
            "Actual neighbor edit did not atomically update cells/derived selection/native "
            "collision");
        const auto alias = detail::runtime_tile_map_snapshot(world.engine.world(), {scene, other});
        check(alias->cells.cell(1, {0, 1}).tile == 3 &&
                  alias->variants->overrides({1, {0, 0}}).data() ==
                      first.variants->overrides({1, {0, 0}}).data(),
              "Editing one instance changed another instance's raw cells or derived overrides");
        detail::release_runtime_tile_edit(world.engine.world(), "project.neighbor_test", token);
    }
    const auto saved_seconds = observed(world).animation_seconds;
    const auto physical = world.physics()->checkpoint();
    const auto maps = detail::checkpoint_runtime_tile_maps(world.engine.world());
    check(maps.at("version") == 2 && maps.at("animation_seconds") == saved_seconds,
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
    const auto recovered_view = observed(recovered);
    check(recovered_view.animation_seconds == saved_seconds, "Recovery reset the animation phase");
    if (neighbors)
        check(recovered_view.cells->cell(1, {}).tile == 3 &&
                  recovered_view.cells->cell(1, {0, 1}).tile == 0 && recovered_view.variants &&
                  tile_map_collision_geometry(*recovered_view.cells, *recovered_view.definitions, 1,
                                              4096, recovered_view.variants.get())
                          .polygons.size() == 1,
              "Recovery did not rebuild selected variants from saved logical cells");
    for (unsigned tick = 0; tick < 20; ++tick) {
        world.simulation.tick(.0625f);
        recovered.simulation.tick(.0625f);
        check(observed(world).animation_seconds == observed(recovered).animation_seconds &&
                  world.physics()->body_state_2d(ref) == recovered.physics()->body_state_2d(ref),
              "Recovered animation/native continuation diverged");
    }
    std::cout
        << "Actual source4/current-cook fixed tile animation, immutable reuse, static collision "
           "and exact recovery PASS\n";
}
