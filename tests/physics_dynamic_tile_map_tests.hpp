#pragma once
#include "runtime_tile_map.hpp"
#include "tile_asset_fixture.hpp"
#include "tile_map_document.hpp"
#include <cmath>
#include <forge/runtime_world.hpp>
#include <thread>

inline void test_dynamic_tile_maps(const std::filesystem::path& root, const forge::Json& original) {
    using namespace forge;
    using namespace forge::detail;
    using namespace std::chrono_literals;
    const auto check = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    for (bool sensor : {false, true})
        for (bool reflected : {false, true})
            for (float requested_mass : {0.f, 12.f}) {
                auto document = original;
                document["entities"] =
                    Json::array({original.at("entities")[0], original.at("entities")[1]});
                for (unsigned i = 0; i < 2; ++i) {
                    auto& row = document["entities"][i];
                    row["spatial"] = {{"mode", "world"}};
                    auto& components = row["components"];
                    components["forge.local_translation"] = {{"x", 30 * i}, {"y", 4}, {"z", 7}};
                    components["forge.local_scale"] = {
                        {"x", reflected ? -1 : 1}, {"y", reflected ? 2 : 1}, {"z", 0}};
                    components["forge.physics_body_2d"] = {{"motion", 2},
                                                           {"sensor", sensor},
                                                           {"density", 2.5},
                                                           {"mass", requested_mass},
                                                           {"allow_sleeping", false}};
                }
                RuntimeWorld world({}, {}, std::nullopt, root, false);
                world.scene.restore_snapshot(document);
                const auto deadline = std::chrono::steady_clock::now() + 15s;
                while (!world.physics()->prepare_assets() || !world.prepare_scene_resources()) {
                    check(std::chrono::steady_clock::now() < deadline,
                          "Dynamic Map preparation stalled");
                    std::this_thread::sleep_for(1ms);
                }
                auto& context = world.engine.world();
                const auto physics = world.physics();
                const auto first_id = document["entities"][0]["id"].get<std::string>();
                const auto second_id = document["entities"][1]["id"].get<std::string>();
                const auto first = world.scene.reference(first_id);
                const auto second = world.scene.reference(second_id);
                const auto initial = runtime_tile_map_snapshot(context, first);
                const auto independent = runtime_tile_map_snapshot(context, second);
                auto epoch = physics->body_state_2d(first)->epoch;
                auto other_epoch = physics->body_state_2d(second)->epoch;
                const double area_mass = 8 * 2.5 * (reflected ? 2 : 1);
                const double mass = requested_mass ? requested_mass : area_mass;
                check(initial && independent && physics->status().at("bodies_2d") == 2 &&
                          physics->status().at("shapes_2d") == 4 &&
                          std::abs(physics->body_state_2d(first)->mass - mass) < 1e-5,
                      "Dynamic Maps lost bounded compound identity or area-density mass");
                world.simulation.tick(1.f / 60);
                check(physics->body_state_2d(first)->position[1] < 4 &&
                          physics->body_state_2d(first)->linear_velocity[1] < 0,
                      "Dynamic solid/sensor Map did not fall under gravity");
                for (const auto& id : {first_id, second_id}) {
                    auto entity = world.scene.entity(id);
                    auto body = entity.get<PhysicsBody2D>();
                    body.gravity_factor = 0;
                    entity.set(body);
                }
                physics->set_linear_velocity_2d(first, {0, 0});
                physics->add_impulse_2d(first, {2 * mass, 0});
                physics->set_angular_velocity_2d(first, .6);
                world.simulation.tick(1.f / 60);
                const auto moving = physics->body_state_2d(first);
                // Effective body configuration changes use the existing replacement
                // policy. Establish the epoch after disabling gravity, then require
                // unchanged geometry to preserve it during subsequent motion.
                epoch = moving->epoch;
                other_epoch = physics->body_state_2d(second)->epoch;
                check(moving->epoch == epoch && moving->angle > 0 &&
                          world.scene.entity(first_id).get<LocalTranslation>().z == 7 &&
                          world.scene.entity(first_id).get<LocalScale>() ==
                              LocalScale{reflected ? -1.f : 1.f, reflected ? 2.f : 1.f, 0} &&
                          runtime_tile_map_snapshot(context, first) == initial,
                      "Dynamic Map motion changed cell authority or presentation scale/depth");
                const auto submit = [&](std::span<const TileMapCellEdit> cells,
                                        RuntimeTileEditState expected) {
                    const auto token = request_runtime_tile_cells(
                        context, "project.dynamic_maps", first,
                        runtime_tile_map_snapshot(context, first)->generation, cells);
                    world.simulation.tick(1.f / 60);
                    const auto result =
                        inspect_runtime_tile_edit(context, "project.dynamic_maps", token);
                    check(result.state == expected, "Dynamic Map cell request outcome differs");
                    check(release_runtime_tile_edit(context, "project.dynamic_maps", token),
                          "Dynamic Map cell result was not releasable");
                };
                const TileMapCellEdit distant{1, {50000, -1}, {2, 0}};
                submit(std::span(&distant, 1), RuntimeTileEditState::Failed);
                check(runtime_tile_map_snapshot(context, first) == initial &&
                          physics->body_state_2d(first)->epoch == epoch,
                      "Rejected Dynamic Map geometry replaced last-good cells/body");
                const TileMapCellEdit extra{1, {10, -1}, {2, 0}};
                submit(std::span(&extra, 1), RuntimeTileEditState::Applied);
                const auto edited = runtime_tile_map_snapshot(context, first);
                const auto replaced = physics->body_state_2d(first);
                const double edited_mass = requested_mass ? requested_mass : area_mass * 9 / 8;
                check(edited != initial && edited->generation == initial->generation &&
                          edited->revision > initial->revision && replaced->epoch > epoch &&
                          std::abs(replaced->mass - edited_mass) < 1e-5 &&
                          std::abs(replaced->linear_velocity[0] - 2) < 1e-5 &&
                          std::abs(replaced->linear_velocity[1]) < 1e-5 &&
                          std::abs(replaced->angular_velocity - .6) < 1e-5,
                      "Dynamic Map edit lost mass update or existing COM/angular velocity policy");
                const auto reset = request_runtime_tile_reset(context, "project.dynamic_maps",
                                                              first, initial->generation);
                world.simulation.tick(1.f / 60);
                const auto reset_cells = runtime_tile_map_snapshot(context, first);
                bool same_cells = reset_cells->cells.occupied() == initial->cells.occupied() &&
                                  !reset_cells->cells.cell(1, {10, -1}).tile;
                for (int x = -3; x < 5; ++x)
                    same_cells &=
                        reset_cells->cells.cell(1, {x, -1}) == initial->cells.cell(1, {x, -1});
                check(inspect_runtime_tile_edit(context, "project.dynamic_maps", reset).state ==
                              RuntimeTileEditState::Applied &&
                          same_cells && reset_cells->generation > initial->generation &&
                          std::abs(physics->body_state_2d(first)->mass - mass) < 1e-5 &&
                          physics->body_state_2d(first)->epoch > replaced->epoch,
                      "Dynamic Map reset did not rebuild original compound mass/cells");
                release_runtime_tile_edit(context, "project.dynamic_maps", reset);
                std::vector<TileMapCellEdit> erase;
                for (int x = -3; x < 5; ++x)
                    erase.push_back({1, {x, -1}, {}});
                submit(erase, RuntimeTileEditState::Applied);
                check(!physics->body_state_2d(first),
                      "Empty Dynamic Map retained a placeholder native body");
                const auto refill = request_runtime_tile_reset(context, "project.dynamic_maps",
                                                               first, reset_cells->generation);
                world.simulation.tick(1.f / 60);
                check(inspect_runtime_tile_edit(context, "project.dynamic_maps", refill).state ==
                              RuntimeTileEditState::Applied &&
                          physics->body_state_2d(first)->epoch > replaced->epoch &&
                          physics->body_state_2d(first)->linear_velocity == Double2{} &&
                          physics->body_state_2d(first)->angular_velocity == 0 &&
                          runtime_tile_map_snapshot(context, second) == independent &&
                          physics->body_state_2d(second)->epoch == other_epoch,
                      "Dynamic Map refill reused retired motion or edited another instance");
                release_runtime_tile_edit(context, "project.dynamic_maps", refill);
                const auto scene = world.scene.snapshot();
                const auto saved_physics = physics->checkpoint();
                const auto saved_maps = checkpoint_runtime_tile_maps(context);
                RuntimeWorld recovered({}, {}, std::nullopt, root, false);
                recovered.scene.restore_snapshot(scene);
                const auto recovery_deadline = std::chrono::steady_clock::now() + 15s;
                for (;;) {
                    recovered.engine.services().resources()->synchronize();
                    synchronize_runtime_tile_maps(recovered.engine.world());
                    if (prepare_runtime_tile_maps(recovered.engine.world()))
                        break;
                    check(std::chrono::steady_clock::now() < recovery_deadline,
                          "Dynamic Map recovery resources stalled");
                    std::this_thread::sleep_for(1ms);
                }
                restore_runtime_tile_maps(recovered.engine.world(), saved_maps);
                recovered.physics()->restore(saved_physics);
                recovered.simulation.restore_input_tick(
                    physics->status().at("tick").get<std::uint64_t>());
                recovered.simulation.reset_presentation();
                for (unsigned tick = 0; tick < 30; ++tick) {
                    world.simulation.tick(1.f / 60);
                    recovered.simulation.tick(1.f / 60);
                    check(recovered.physics()->body_state_2d(first) ==
                                  physics->body_state_2d(first) &&
                              recovered.physics()->body_state_2d(second) ==
                                  physics->body_state_2d(second),
                          "Fresh Dynamic Map runtime recovery continuation diverged");
                }
            }
}

inline void test_dynamic_tile_map_replacement(const std::filesystem::path& root,
                                              const forge::Json& original,
                                              forge::TileMapDocument& map,
                                              forge::AssetImportService& importer) {
    using namespace forge;
    using namespace forge::detail;
    using namespace std::chrono_literals;
    const auto check = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    auto document = original;
    document["entities"] = Json::array({original.at("entities")[0], original.at("entities")[1]});
    for (unsigned i = 0; i < 2; ++i) {
        auto& row = document["entities"][i];
        row["spatial"] = {{"mode", "world"}};
        row["components"]["forge.local_translation"] = {{"x", 30 * i}, {"y", 0}, {"z", 7}};
        row["components"]["forge.local_scale"] = {{"x", 1}, {"y", 1}, {"z", 0}};
        row["components"]["forge.physics_body_2d"] = {
            {"motion", 2}, {"gravity_factor", 0}, {"density", 2.5}, {"allow_sleeping", false}};
    }
    RuntimeWorld world({}, {}, std::nullopt, root, false);
    world.scene.restore_snapshot(document);
    const auto deadline = std::chrono::steady_clock::now() + 15s;
    while (!world.physics()->prepare_assets() || !world.prepare_scene_resources()) {
        check(std::chrono::steady_clock::now() < deadline,
              "Dynamic replacement fixture preparation stalled");
        std::this_thread::sleep_for(1ms);
    }
    auto& context = world.engine.world();
    const auto physics = world.physics();
    const auto a = world.scene.reference(document["entities"][0]["id"].get<std::string>());
    const auto b = world.scene.reference(document["entities"][1]["id"].get<std::string>());
    const auto initial = runtime_tile_map_snapshot(context, a);
    physics->set_linear_velocity_2d(a, {1, 0});
    physics->set_linear_velocity_2d(b, {2, 0});
    physics->set_angular_velocity_2d(a, .25);
    physics->set_angular_velocity_2d(b, .5);
    const TileMapCellEdit instance_cell{1, {12, -1}, {2, 0}};
    const auto edit = request_runtime_tile_cells(context, "project.dynamic_replace", a,
                                                 initial->generation, std::span(&instance_cell, 1));
    world.simulation.tick(1.f / 60);
    check(inspect_runtime_tile_edit(context, "project.dynamic_replace", edit).state ==
              RuntimeTileEditState::Applied,
          "Dynamic replacement fixture edit failed");
    release_runtime_tile_edit(context, "project.dynamic_replace", edit);
    const auto edited = runtime_tile_map_snapshot(context, a);
    const auto untouched = runtime_tile_map_snapshot(context, b);
    const auto epoch_a = physics->body_state_2d(a)->epoch;
    const auto epoch_b = physics->body_state_2d(b)->epoch;
    const auto publish = [&] {
        map.save();
        check(test_tile_asset_io::publish(importer, map.locator(), map.source().asset(), true)
                  .published,
              "Dynamic replacement fixture source publication failed");
        world.engine.services().resources()->refresh();
    };
    const auto wait = [&](auto&& ready) {
        const auto end = std::chrono::steady_clock::now() + 15s;
        while (!ready()) {
            (void)world.simulation.presentation(0);
            check(std::chrono::steady_clock::now() < end,
                  "Dynamic replacement fixture resource transition stalled");
            std::this_thread::sleep_for(1ms);
        }
    };
    map.edit_source(map.revision(), "Physically unsupported source candidate", [](auto& source) {
        const TileMapCellEdit cell{1, {50000, -1}, {2, 0}};
        source.document = source.document.paint(std::span(&cell, 1));
    });
    publish();
    wait([&] {
        const auto status = inspect_runtime_tile_map(context, b);
        return status.ready && !status.current &&
               status.diagnostic.find("collision") != std::string::npos;
    });
    world.simulation.tick(1.f / 60);
    check(runtime_tile_map_snapshot(context, a) == edited &&
              runtime_tile_map_snapshot(context, b) == untouched &&
              physics->body_state_2d(a)->epoch == epoch_a &&
              physics->body_state_2d(b)->epoch == epoch_b,
          "Rejected Dynamic source candidate replaced edited or last-good cells/native bodies");
    check(map.can_undo(), "Dynamic replacement fixture source Undo unavailable");
    map.undo();
    check(!map.source().document.cell(1, {50000, -1}).tile,
          "Dynamic replacement fixture source Undo failed");
    publish();
    wait([&] { return inspect_runtime_tile_map(context, b).current; });
    map.edit_source(map.revision(), "Admitted source geometry replacement", [](auto& source) {
        const TileMapCellEdit cell{1, {10, -1}, {2, 0}};
        source.document = source.document.paint(std::span(&cell, 1));
    });
    publish();
    wait([&] {
        const auto status = inspect_runtime_tile_map(context, b);
        return status.ready && status.current && status.generation > untouched->generation;
    });
    world.simulation.tick(1.f / 60);
    check(runtime_tile_map_snapshot(context, a) == edited &&
              physics->body_state_2d(a)->epoch == epoch_a &&
              runtime_tile_map_snapshot(context, b)->cells.cell(1, {10, -1}).tile == 2 &&
              physics->body_state_2d(b)->epoch > epoch_b &&
              std::abs(physics->body_state_2d(b)->mass - 22.5) < 1e-5 &&
              std::abs(physics->body_state_2d(b)->linear_velocity[0] - 2) < 1e-5 &&
              std::abs(physics->body_state_2d(b)->angular_velocity - .5) < 1e-5,
          "Dynamic source replacement lost mass/velocity or overwrote another instance's edits");
    const auto reset =
        request_runtime_tile_reset(context, "project.dynamic_replace", a, edited->generation);
    world.simulation.tick(1.f / 60);
    const auto reset_cells = runtime_tile_map_snapshot(context, a);
    check(inspect_runtime_tile_edit(context, "project.dynamic_replace", reset).state ==
                  RuntimeTileEditState::Applied &&
              reset_cells->generation > edited->generation &&
              reset_cells->cells.cell(1, {10, -1}).tile == 2 &&
              !reset_cells->cells.cell(1, {12, -1}).tile &&
              physics->body_state_2d(a)->epoch > epoch_a &&
              std::abs(physics->body_state_2d(a)->linear_velocity[0] - 1) < 1e-5 &&
              std::abs(physics->body_state_2d(a)->angular_velocity - .25) < 1e-5,
          "Explicit Dynamic Map reset did not adopt new source through existing generation/state "
          "policy");
    release_runtime_tile_edit(context, "project.dynamic_replace", reset);
    check(map.can_undo(), "Dynamic replacement fixture restoration Undo unavailable");
    map.undo();
    check(!map.source().document.cell(1, {10, -1}).tile,
          "Dynamic replacement fixture restoration Undo failed");
    publish();
}
