#pragma once
#include "asset_bytes.hpp"
#include "builtins.hpp"
#include "runtime_tile_map.hpp"
#include "tile_asset_fixture.hpp"
#include "tile_asset_worker.hpp"
#include "tile_map_document.hpp"
#include <forge/runtime_world.hpp>
#include <thread>

inline void test_physics_tile_map(const std::filesystem::path& root,
                                  std::shared_ptr<forge::ProjectLease> lease,
                                  const std::filesystem::path& worker, forge::AssetId set) {
    using namespace forge;
    using namespace forge::detail;
    using namespace std::chrono_literals;
    const auto check = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    const auto defaults = [](const char* name) -> Json {
        for (const auto& type : detail::builtins())
            if (std::string_view(type.name) == name)
                return type.defaults;
        throw std::runtime_error("Physics Map fixture builtin is unavailable");
    };
    auto map = TileMapDocument::create(lease, "Assets/Physics.tilemap.json", [&](AssetId id) {
        return TileMapSource{TileMapValue::create(id, set)};
    });
    map->edit_source(map->revision(), "Original painted floor and decoration", [](auto& source) {
        auto metadata = source.document.metadata();
        metadata["next_id"] = 3;
        metadata["layers"].push_back(
            {{"id", 2}, {"name", "Decoration"}, {"visible", false}, {"order", 1}});
        source.document = source.document.with_metadata(std::move(metadata));
        std::vector<TileMapCellEdit> cells;
        for (int x = -3; x < 5; ++x)
            cells.push_back({1, {x, -1}, {2, unsigned(x + 3)}});
        cells.push_back({2, {INT32_MAX, INT32_MIN}, {2, 7}});
        source.document = source.document.paint(cells);
    });
    map->save();
    const auto original_source = map->source().document;
    const auto original_bytes =
        asset_detail::read_bytes(root / map->locator(), tile_map_source_byte_limit);
    AssetImportService importer(lease, tile_asset_import_registry(worker),
                                {"linux", "none", "cpu"});
    const auto published =
        test_tile_asset_io::publish(importer, map->locator(), map->source().asset(), true);
    check(published.published, "Physics Tile Map publication failed");
    const auto map_a = EntityId::generate(), map_b = EntityId::generate();
    const auto actor_a = EntityId::generate(), actor_b = EntityId::generate();
    const auto connection = EntityId::generate();
    auto floor = [&](EntityId id, double x, bool reflected) {
        auto renderer = defaults("forge.tile_map_renderer");
        renderer["map"] = map->source().asset();
        renderer["enabled"] = !reflected;
        return Json{
            {"id", id},
            {"name", "Painted floor"},
            {"components",
             {{"forge.tile_map_renderer", renderer},
              {"forge.physics_body_2d", Json::object()},
              {"forge.tile_map_collider_2d", {{"layer", 1}}},
              {"forge.local_translation", {{"x", x}, {"y", 0}, {"z", 7}}},
              {"forge.local_scale",
               {{"x", reflected ? -1 : 1}, {"y", reflected ? 2 : 1}, {"z", reflected ? 0 : 1}}}}}};
    };
    auto actor = [](EntityId id, double x) {
        return Json{{"id", id},
                    {"name", "Falling actor"},
                    {"spatial", {{"mode", "world"}}},
                    {"components",
                     {{"forge.physics_body_2d", {{"motion", 2}}},
                      {"forge.circle_collider_2d", {{"radius", .25}}},
                      {"forge.local_translation", {{"x", x}, {"y", 3}, {"z", 7}}}}}};
    };
    Json document{{"version", 5},
                  {"asset_id", AssetId::generate()},
                  {"entities", Json::array({floor(map_a, 0, true), floor(map_b, 12, false),
                                            actor(actor_a, 0), actor(actor_b, 12)})}};
    const auto scene_id = document.at("asset_id").get<AssetId>();
    auto connection_value = defaults("forge.physics_joint_2d");
    connection_value["enabled"] = false;
    connection_value["body_a"] = EntityRef{scene_id, map_a};
    connection_value["body_b"] = EntityRef{scene_id, actor_a};
    connection_value["distance"] = .25;
    document["entities"].push_back(
        {{"id", connection},
         {"name", "Optional floor connection"},
         {"components", {{"forge.physics_joint_2d", connection_value}}}});
    RuntimeWorld world({}, {}, std::nullopt, root, false);
    world.scene.restore_snapshot(document);
    const auto deadline = std::chrono::steady_clock::now() + 15s;
    while (!world.physics()->prepare_assets() || !world.prepare_scene_resources()) {
        check(std::chrono::steady_clock::now() < deadline, "Physics Map preparation stalled");
        std::this_thread::sleep_for(1ms);
    }
    auto& context = world.engine.world();
    const auto a = world.scene.reference(map_a.str()), b = world.scene.reference(map_b.str());
    const auto dynamic_a = world.scene.reference(actor_a.str()),
               dynamic_b = world.scene.reference(actor_b.str());
    check(world.physics()->status().at("bodies_2d") == 4 &&
              world.physics()->status().at("shapes_2d") == 6,
          "Painted floor did not use two compound bodies and chunk rectangles");
    const auto initial = runtime_tile_map_snapshot(context, a);
    const auto epoch_a = world.physics()->body_state_2d(a)->epoch;
    const auto epoch_b = world.physics()->body_state_2d(b)->epoch;
    for (unsigned tick = 0; tick < 240; ++tick)
        world.simulation.tick(1.f / 60);
    check(std::abs(world.physics()->body_state_2d(dynamic_a)->position[1] - .25) < .02 &&
              std::abs(world.physics()->body_state_2d(dynamic_b)->position[1] - .25) < .02,
          "Actors did not land on their independent painted floors");
    check(world.scene.entity(map_a.str()).get<LocalScale>() == LocalScale{-1, 2, 0},
          "Map collision changed signed, nonuniform or zero-depth presentation scale");
    PhysicsOverlap2D query;
    query.shape = PhysicsOverlap2D::Shape::Box;
    query.origin = {0, -1};
    query.dimensions = {8, .5};
    check(world.physics()->overlap_shape_2d(query, {}) == std::vector<EntityRef>{a},
          "Compound Map query duplicated cells or used decorative-layer extents");
    const auto hit = world.physics()->raycast_2d({1.5, 2}, {0, -4});
    check(hit && hit->entity == a && std::abs(hit->fraction - .5) < .001,
          "Painted floor ray did not use the shared collision instance");
    PhysicsSweep2D sweep;
    sweep.origin = {1.5, 2};
    sweep.displacement = {0, -4};
    sweep.dimensions = {.2, 0};
    const auto swept = world.physics()->shape_cast_2d(sweep);
    check(swept && swept->entity == a && swept->fraction > .4 && swept->fraction < .5 &&
              swept->normal[1] > .99,
          "Swept shape did not use the compound painted floor");
    sweep.ignore = a;
    check(!world.physics()->shape_cast_2d(sweep),
          "Canonical sweep exclusion retained a compound floor shape");
    const std::array<TileMapCellEdit, 1> distant{{{1, {50000, -1}, {2, 0}}}};
    const auto invalid_extent = request_runtime_tile_cells(context, "project.physics_tiles", a,
                                                           initial->generation, distant);
    world.simulation.tick(1.f / 60);
    check(inspect_runtime_tile_edit(context, "project.physics_tiles", invalid_extent).state ==
                  RuntimeTileEditState::Failed &&
              runtime_tile_map_snapshot(context, a) == initial &&
              world.physics()->body_state_2d(a)->epoch == epoch_a,
          "Unsupported cell extent was published before collision admission");
    release_runtime_tile_edit(context, "project.physics_tiles", invalid_extent);
    std::vector<TileMapCellEdit> crowded;
    for (int y = 0; y < 64 && crowded.size() < 4092; ++y)
        for (int x = 0; x < 128 && crowded.size() < 4092; ++x)
            if ((x + y) % 2 == 0)
                crowded.push_back({1, {x, y + 4}, {2, 0}});
    const auto invalid_capacity = request_runtime_tile_cells(context, "project.physics_tiles", a,
                                                             initial->generation, crowded);
    world.simulation.tick(1.f / 60);
    check(inspect_runtime_tile_edit(context, "project.physics_tiles", invalid_capacity).state ==
                  RuntimeTileEditState::Failed &&
              runtime_tile_map_snapshot(context, a) == initial &&
              world.physics()->status().at("shapes_2d") == 6 &&
              world.physics()->body_state_2d(a)->epoch == epoch_a,
          "Per-world compound shape capacity published partial cells or native geometry");
    release_runtime_tile_edit(context, "project.physics_tiles", invalid_capacity);
    std::vector<TileMapCellEdit> erase, fill;
    for (int x = -3; x < 5; ++x) {
        erase.push_back({1, {x, -1}, {}});
        fill.push_back({1, {x, -1}, {2, 0}});
    }
    auto joint = world.scene.entity(connection.str()).get<PhysicsJoint2D>();
    joint.enabled = true;
    world.scene.entity(connection.str()).set<PhysicsJoint2D>(joint);
    world.simulation.tick(1.f / 60);
    const auto invalid_connection =
        request_runtime_tile_cells(context, "project.physics_tiles", a, initial->generation, erase);
    world.simulation.tick(1.f / 60);
    check(inspect_runtime_tile_edit(context, "project.physics_tiles", invalid_connection).state ==
                  RuntimeTileEditState::Failed &&
              runtime_tile_map_snapshot(context, a) == initial &&
              world.physics()->status().at("joints_2d") == 1 &&
              world.physics()->body_state_2d(a)->epoch == epoch_a,
          "Cell edit removed an enabled joint's collision endpoint before admission");
    release_runtime_tile_edit(context, "project.physics_tiles", invalid_connection);
    joint.enabled = false;
    world.scene.entity(connection.str()).set<PhysicsJoint2D>(joint);
    const auto token =
        request_runtime_tile_cells(context, "project.physics_tiles", a, initial->generation, erase);
    (void)world.simulation.presentation(0);
    check(world.physics()->status().at("shapes_2d") == 6,
          "Paused cell request changed native geometry");
    world.simulation.tick(1.f / 60);
    check(inspect_runtime_tile_edit(context, "project.physics_tiles", token).state ==
                  RuntimeTileEditState::Applied &&
              !world.physics()->body_state_2d(a) &&
              world.physics()->status().at("shapes_2d") == 4 &&
              world.physics()->body_state_2d(b)->epoch == epoch_b &&
              runtime_tile_map_snapshot(context, b)->cells.cell(1, {-1, -1}).tile == 2,
          "Erasing the last solid cells retained phantom collision or changed another instance");
    release_runtime_tile_edit(context, "project.physics_tiles", token);
    for (unsigned tick = 0; tick < 30; ++tick)
        world.simulation.tick(1.f / 60);
    check(world.physics()->body_state_2d(dynamic_a)->position[1] < -.3 &&
              std::abs(world.physics()->body_state_2d(dynamic_b)->position[1] - .25) < .02,
          "Removed Map collision did not release only its own actor");
    const auto scene_snapshot = world.scene.snapshot();
    const auto physics_snapshot = world.physics()->checkpoint();
    const auto maps_snapshot = checkpoint_runtime_tile_maps(context);
    RuntimeWorld recovered({}, {}, std::nullopt, root, false);
    recovered.scene.restore_snapshot(scene_snapshot);
    const auto recovery_deadline = std::chrono::steady_clock::now() + 15s;
    for (;;) {
        recovered.engine.services().resources()->synchronize();
        synchronize_runtime_tile_maps(recovered.engine.world());
        if (prepare_runtime_tile_maps(recovered.engine.world()))
            break;
        check(std::chrono::steady_clock::now() < recovery_deadline,
              "Map recovery preparation stalled");
        std::this_thread::sleep_for(1ms);
    }
    restore_runtime_tile_maps(recovered.engine.world(), maps_snapshot);
    recovered.physics()->restore(physics_snapshot);
    recovered.simulation.restore_input_tick(
        world.physics()->status().at("tick").get<std::uint64_t>());
    recovered.simulation.reset_presentation();
    check(!recovered.physics()->body_state_2d(a) && recovered.physics()->body_state_2d(dynamic_a) ==
                                                        world.physics()->body_state_2d(dynamic_a),
          "Fresh physics restored authored cells instead of the edited empty layer");
    for (unsigned tick = 0; tick < 10; ++tick) {
        world.simulation.tick(1.f / 60);
        recovered.simulation.tick(1.f / 60);
        check(recovered.physics()->body_state_2d(dynamic_a) ==
                  world.physics()->body_state_2d(dynamic_a),
              "Recovered empty Map/compound world diverged during continuation");
    }
    const auto refill =
        request_runtime_tile_cells(context, "project.physics_tiles", a, initial->generation, fill);
    world.physics()->teleport_2d(dynamic_a, {0, 3}, 0, true);
    world.simulation.tick(1.f / 60);
    check(inspect_runtime_tile_edit(context, "project.physics_tiles", refill).state ==
                  RuntimeTileEditState::Applied &&
              world.physics()->body_state_2d(a) &&
              world.physics()->body_state_2d(a)->epoch > epoch_a,
          "Painting an empty collision layer did not create a new native realization");
    release_runtime_tile_edit(context, "project.physics_tiles", refill);
    for (unsigned tick = 0; tick < 180; ++tick)
        world.simulation.tick(1.f / 60);
    check(std::abs(world.physics()->body_state_2d(dynamic_a)->position[1] - .25) < .02 &&
              world.physics()->body_state_2d(b)->epoch == epoch_b &&
              map->source().document == original_source &&
              asset_detail::read_bytes(root / map->locator(), tile_map_source_byte_limit) ==
                  original_bytes,
          "Refilled floor failed to catch its actor or changed another instance/source");
    map->edit_source(map->revision(), "Physically unsupported published cell", [](auto& source) {
        const std::array<TileMapCellEdit, 1> distant{{{1, {50000, -1}, {2, 0}}}};
        source.document = source.document.paint(distant);
    });
    map->save();
    check(test_tile_asset_io::publish(importer, map->locator(), map->source().asset(), true)
              .published,
          "Ordinary Map source rejected a valid cell outside the bounded physics profile");
    world.engine.services().resources()->refresh();
    const auto replacement_deadline = std::chrono::steady_clock::now() + 15s;
    for (;;) {
        (void)world.simulation.presentation(0);
        const auto status = inspect_runtime_tile_map(context, b);
        if (status.ready && !status.current &&
            status.diagnostic.find("collision") != std::string::npos)
            break;
        check(std::chrono::steady_clock::now() < replacement_deadline,
              "Physics Map replacement did not retain its last admitted snapshot");
        std::this_thread::sleep_for(1ms);
    }
    for (unsigned tick = 0; tick < 6; ++tick)
        world.simulation.tick(1.f / 60);
    check(world.physics()->body_state_2d(b)->epoch == epoch_b &&
              std::abs(world.physics()->body_state_2d(dynamic_b)->position[1] - .25) < .02,
          "Physically unsupported source replacement changed last-good native collision");
    map->undo();
    map->save();
    check(test_tile_asset_io::publish(importer, map->locator(), map->source().asset(), true)
              .published,
          "Restored original Physics Map failed publication");
    world.engine.services().resources()->refresh();
    const auto restore_deadline = std::chrono::steady_clock::now() + 15s;
    while (!inspect_runtime_tile_map(context, a).current ||
           !inspect_runtime_tile_map(context, b).current) {
        (void)world.simulation.presentation(0);
        check(std::chrono::steady_clock::now() < restore_deadline,
              "Restored Map source did not become current");
        std::this_thread::sleep_for(1ms);
    }
    check(asset_detail::read_bytes(root / map->locator(), tile_map_source_byte_limit) ==
                  original_bytes &&
              world.physics()->body_state_2d(b)->epoch == epoch_b,
          "Source restoration rewrote original Map bytes or unchanged native geometry");
}
