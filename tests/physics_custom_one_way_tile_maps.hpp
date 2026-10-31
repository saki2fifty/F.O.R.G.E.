#pragma once
#include "asset_storage.hpp"
#include "runtime_tile_map.hpp"
#include "tile_asset_fixture.hpp"
#include "tile_map_document.hpp"
#include "tile_set_document.hpp"
#include <array>
#include <cmath>
#include <forge/runtime_world.hpp>
#include <thread>

inline void test_custom_one_way_tile_maps(const std::filesystem::path& root,
                                          std::shared_ptr<forge::ProjectLease> lease,
                                          const std::filesystem::path& worker,
                                          forge::AssetId original_set,
                                          const forge::Json& original) {
    using namespace forge;
    using namespace forge::detail;
    using namespace std::chrono_literals;
    using test_tile_asset_io::check;
    const auto catalog = AssetCatalog::open_project(root);
    const auto old = TileSetSource::parse(asset_detail::read_bytes(
        root / catalog.records().at(original_set).source, tile_set_source_byte_limit));
    auto definitions =
        TileSetDocument::create(lease, "Assets/OneWayContours.tileset.json", [&](AssetId id) {
            auto source = old;
            source.document["asset_id"] = id;
            source.set_collision(
                2,
                {{"kind", "contours"},
                 {"contours",
                  Json::array({{{"points", {{0, 0}, {1, 0}, {1, 1}, {0, 1}}},
                                {"holes", Json::array({{{.2, .2}, {.8, .2}, {.8, .8}, {.2, .8}}})}},
                               {{"points", {{.4, .4}, {.6, .4}, {.6, .6}, {.4, .6}}},
                                {"holes", Json::array()}}})}});
            return source;
        });
    definitions->save();
    AssetImportService importer(lease, tile_asset_import_registry(worker),
                                {"linux", "none", "cpu"});
    const auto set_publication = test_tile_asset_io::publish(importer, definitions->locator(),
                                                             definitions->source().asset(), false);
    check(set_publication.published,
          "One-way contour definition publication failed: " + set_publication.diagnostic);
    for (unsigned motion : {0u, 1u}) {
        auto map = TileMapDocument::create(
            lease, "Assets/OneWayContours" + std::to_string(motion) + ".tilemap.json",
            [&](AssetId id) {
                auto value = TileMapValue::create(id, definitions->source().asset());
                auto metadata = value.metadata();
                metadata["cell_size"] = {4, 4};
                value = value.with_metadata(metadata);
                const TileMapCellEdit cell{1, {0, 0}, {2, 0}};
                return TileMapSource{value.paint(std::span(&cell, 1))};
            });
        const auto publish = [&] {
            map->save();
            check(test_tile_asset_io::publish(importer, map->locator(), map->source().asset(), true)
                      .published,
                  "One-way contour Map publication failed");
        };
        publish();
        auto document = original;
        document["entities"] = Json::array();
        std::array<EntityId, 2> maps, actors;
        for (unsigned i = 0; i < 2; ++i) {
            auto row = original.at("entities")[1];
            maps[i] = EntityId::generate();
            row["id"] = maps[i];
            row["components"]["forge.tile_map_renderer"]["map"] = map->source().asset();
            row["components"]["forge.local_translation"] = {{"x", 20 * i}, {"y", 0}, {"z", 7}};
            row["components"]["forge.local_scale"] = {{"x", 1}, {"y", 1}, {"z", 1}};
            row["components"]["forge.physics_body_2d"] = {{"motion", motion}, {"one_way", true}};
            document["entities"].push_back(std::move(row));
            row = original.at("entities")[3];
            actors[i] = EntityId::generate();
            row["id"] = actors[i];
            row["components"]["forge.local_translation"] = {{"x", 20 * i + .4}, {"y", 6}, {"z", 7}};
            document["entities"].push_back(std::move(row));
        }
        RuntimeWorld world({}, {}, std::nullopt, root, false);
        world.scene.restore_snapshot(document);
        const auto deadline = std::chrono::steady_clock::now() + 15s;
        while (!world.physics()->prepare_assets() || !world.prepare_scene_resources()) {
            check(std::chrono::steady_clock::now() < deadline,
                  "One-way contour Map preparation stalled");
            std::this_thread::sleep_for(1ms);
        }
        auto& context = world.engine.world();
        auto physics = world.physics();
        const auto a = world.scene.reference(maps[0].str()),
                   b = world.scene.reference(maps[1].str());
        for (unsigned tick = 0; tick < 120; ++tick)
            world.simulation.tick(1.f / 60);
        for (auto actor : actors) {
            const auto state = physics->body_state_2d(world.scene.reference(actor.str()));
            check(state && std::abs(state->position[1] - 4.25) < .03,
                  "Published custom one-way Map failed actual runtime landing");
        }
        const auto initial = runtime_tile_map_snapshot(context, a);
        const auto other = runtime_tile_map_snapshot(context, b);
        auto epoch_a = physics->body_state_2d(a)->epoch;
        const auto epoch_b = physics->body_state_2d(b)->epoch;
        const auto submit = [&](TileMapCellEdit cell, RuntimeTileEditState expected) {
            const auto token = request_runtime_tile_cells(
                context, "project.custom_one_way", a,
                runtime_tile_map_snapshot(context, a)->generation, std::span(&cell, 1));
            world.simulation.tick(1.f / 60);
            check(inspect_runtime_tile_edit(context, "project.custom_one_way", token).state ==
                      expected,
                  "Custom one-way cell request returned the wrong outcome");
            check(release_runtime_tile_edit(context, "project.custom_one_way", token),
                  "Custom one-way cell request result did not release");
        };
        submit({1, {50000, 0}, {2, 0}}, RuntimeTileEditState::Failed);
        check(runtime_tile_map_snapshot(context, a) == initial &&
                  physics->body_state_2d(a)->epoch == epoch_a &&
                  runtime_tile_map_snapshot(context, b) == other &&
                  physics->body_state_2d(b)->epoch == epoch_b,
              "Rejected custom one-way cells changed last-good geometry or another instance");
        submit({1, {2, 0}, {2, 7}}, RuntimeTileEditState::Applied);
        const auto edited = runtime_tile_map_snapshot(context, a);
        check(edited != initial && edited->generation == initial->generation &&
                  edited->revision > initial->revision &&
                  physics->body_state_2d(a)->epoch > epoch_a &&
                  runtime_tile_map_snapshot(context, b) == other,
              "Custom one-way cell replacement lost its instance/revision/native epoch policy");
        epoch_a = physics->body_state_2d(a)->epoch;
        const auto wait = [&](const char* stage, auto&& ready) {
            const auto end = std::chrono::steady_clock::now() + 15s;
            while (!ready()) {
                (void)world.simulation.presentation(0);
                check(std::chrono::steady_clock::now() < end,
                      "Custom one-way source transition stalled at " + std::string(stage) + ": " +
                          inspect_runtime_tile_map(context, b).diagnostic);
                std::this_thread::sleep_for(1ms);
            }
        };
        const auto refresh = [&] {
            publish();
            world.engine.services().resources()->refresh();
        };
        map->edit_source(map->revision(), "Unsupported custom one-way source extent",
                         [](auto& source) {
                             const TileMapCellEdit cell{1, {50000, 0}, {2, 0}};
                             source.document = source.document.paint(std::span(&cell, 1));
                         });
        refresh();
        wait("rejected extent", [&] {
            const auto status = inspect_runtime_tile_map(context, b);
            return status.ready && !status.current &&
                   status.diagnostic.find("extent") != std::string::npos;
        });
        world.simulation.tick(1.f / 60);
        check(runtime_tile_map_snapshot(context, a) == edited &&
                  runtime_tile_map_snapshot(context, b) == other &&
                  physics->body_state_2d(a)->epoch == epoch_a &&
                  physics->body_state_2d(b)->epoch == epoch_b,
              "Rejected custom one-way source revision replaced edited or last-good bodies");
        check(map->can_undo(), "Custom one-way source Undo unavailable");
        map->undo();
        refresh();
        wait("restored source", [&] { return inspect_runtime_tile_map(context, b).current; });
        map->edit_source(map->revision(), "Admitted custom one-way source replacement",
                         [](auto& source) {
                             const TileMapCellEdit cell{1, {3, 0}, {2, 5}};
                             source.document = source.document.paint(std::span(&cell, 1));
                         });
        refresh();
        wait("admitted source", [&] {
            const auto status = inspect_runtime_tile_map(context, b);
            return status.ready && status.current && status.generation > other->generation;
        });
        world.simulation.tick(1.f / 60);
        check(runtime_tile_map_snapshot(context, a) == edited &&
                  physics->body_state_2d(a)->epoch == epoch_a &&
                  runtime_tile_map_snapshot(context, b)->cells.cell(1, {3, 0}).tile == 2 &&
                  physics->body_state_2d(b)->epoch > epoch_b,
              "Custom one-way source replacement overwrote edited cells or retained obsolete "
              "geometry");
        const auto reset =
            request_runtime_tile_reset(context, "project.custom_one_way", a, edited->generation);
        world.simulation.tick(1.f / 60);
        const auto adopted = runtime_tile_map_snapshot(context, a);
        check(inspect_runtime_tile_edit(context, "project.custom_one_way", reset).state ==
                      RuntimeTileEditState::Applied &&
                  adopted->generation > edited->generation &&
                  adopted->cells.cell(1, {3, 0}).tile == 2 &&
                  !adopted->cells.cell(1, {2, 0}).tile &&
                  physics->body_state_2d(a)->epoch > epoch_a,
              "Custom one-way Reset failed to adopt current source with a fresh generation");
        check(release_runtime_tile_edit(context, "project.custom_one_way", reset),
              "Custom one-way Reset result did not release");
    }
}
