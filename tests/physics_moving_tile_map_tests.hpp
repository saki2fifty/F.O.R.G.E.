#pragma once
#include "physics_2d.hpp"
#include "runtime_tile_map.hpp"
#include <cmath>
#include <forge/runtime_world.hpp>
#include <thread>
#include <tuple>

// The caller owns publication. Every case borrows the same published Map and uses
// normal RuntimeWorld, Map preflight, body FIFO, native events and recovery owners.
inline void test_moving_trigger_tile_maps(const std::filesystem::path& root,
                                          const forge::Json& original) {
    using namespace forge;
    using namespace forge::detail;
    using namespace std::chrono_literals;
    const auto check = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    const auto prepare = [&](RuntimeWorld& world) {
        const auto deadline = std::chrono::steady_clock::now() + 15s;
        while (!world.physics()->prepare_assets() || !world.prepare_scene_resources()) {
            check(std::chrono::steady_clock::now() < deadline,
                  "Moving/trigger Map preparation stalled");
            std::this_thread::sleep_for(1ms);
        }
    };
    for (const auto& [motion, sensor, one_way, sign] :
         {std::tuple{0u, false, false, 1.f}, std::tuple{0u, true, false, 1.f},
          std::tuple{1u, false, false, 1.f}, std::tuple{1u, true, false, 1.f},
          std::tuple{0u, false, true, 1.f}, std::tuple{0u, false, true, -1.f},
          std::tuple{1u, false, true, 1.f}, std::tuple{1u, false, true, -1.f}}) {
        auto document = original;
        document["entities"] =
            Json::array({original.at("entities")[1], original.at("entities")[3]});
        auto& map = document["entities"][0]["components"];
        map["forge.local_translation"] = {{"x", 0}, {"y", 0}, {"z", 7}};
        map["forge.local_scale"] = {{"x", 1}, {"y", sign}, {"z", 0}};
        map["forge.physics_body_2d"] = {
            {"motion", motion}, {"sensor", sensor}, {"one_way", one_way}};
        auto& visitor = document["entities"][1]["components"];
        visitor["forge.local_translation"] = {
            {"x", .1}, {"y", sign * (sensor ? -.5 : 3)}, {"z", 7}};
        visitor["forge.physics_body_2d"]["gravity_factor"] = sensor ? 0 : sign;
        const auto map_id = document["entities"][0]["id"].get<std::string>();
        const auto visitor_id = document["entities"][1]["id"].get<std::string>();
        RuntimeWorld world({}, {}, std::nullopt, root, false);
        world.scene.restore_snapshot(document);
        prepare(world);
        auto physics = world.physics();
        auto& context = world.engine.world();
        const auto floor = world.scene.reference(map_id), actor = world.scene.reference(visitor_id);
        const auto cells = runtime_tile_map_snapshot(context, floor);
        const auto epoch = physics->body_state_2d(floor)->epoch;
        check(physics->status().at("shapes_2d") == 3,
              "Moving/trigger Map lost its two cross-chunk rectangles");
        const auto event = [&](bool begin, bool available, std::uint64_t expected_epoch) {
            bool found = false;
            for (std::size_t i = 0; i < physics->contact_count_2d(); ++i) {
                const auto copied = physics->contact_event_2d(i);
                if (copied && copied->sensor && copied->first == floor && copied->second == actor &&
                    copied->begin == begin) {
                    check(copied->first_epoch == expected_epoch &&
                              copied->first_available == available && copied->second_available,
                          "Map sensor event resolved through a replaced native epoch");
                    found = true;
                }
            }
            return found;
        };
        world.simulation.tick(1.f / 60);
        if (sensor)
            check(event(true, true, epoch) &&
                      std::abs(physics->body_state_2d(actor)->position[1] + .5) < 1e-6,
                  "Map sensor did not report Begin without solid response");
        else {
            for (unsigned i = 0; i < 180; ++i)
                world.simulation.tick(1.f / 60);
            check(std::abs(physics->body_state_2d(actor)->position[1] - sign * .25) < .02,
                  "Solid Static/Kinematic Map did not support its falling visitor");
        }
        double angle = 0, tx = 0, ty = 0;
        if (motion == 1) {
            tx = 2;
            ty = 1;
            angle = .25;
            physics->move_kinematic_2d(floor, {tx, ty}, angle);
            world.simulation.tick(1.f / 60);
            const auto moved = physics->body_state_2d(floor);
            const bool accepted =
                moved && moved->epoch == epoch &&
                std::hypot(moved->position[0] - tx, moved->position[1] - ty) < 1e-5 &&
                // Native target rotation is integrated over four substeps,
                // not teleported to the exact authored quaternion.
                std::abs(moved->angle - angle) < .001 &&
                runtime_tile_map_snapshot(context, floor) == cells &&
                world.scene.entity(map_id).get<LocalScale>() == LocalScale{1, sign, 0} &&
                world.scene.entity(map_id).get<LocalTranslation>().z == 7;
            if (!accepted)
                throw std::runtime_error(
                    "Kinematic Map target: present=" + std::to_string(bool(moved)) +
                    ", epoch=" + std::to_string(moved ? moved->epoch : 0) +
                    ", expected_epoch=" + std::to_string(epoch) +
                    ", x=" + std::to_string(moved ? moved->position[0] : 0) +
                    ", y=" + std::to_string(moved ? moved->position[1] : 0) +
                    ", angle=" + std::to_string(moved ? moved->angle : 0) + ", cells_unchanged=" +
                    std::to_string(runtime_tile_map_snapshot(context, floor) == cells));
        }
        // Keep the visitor out of query paths, then check rotated native geometry.
        physics->teleport_2d(actor, {100, 100}, 0, true);
        world.simulation.tick(1.f / 60);
        angle = physics->body_state_2d(floor)->angle;
        if (sensor && !motion)
            check(event(false, true, epoch), "Static Map sensor did not report visitor End");
        const auto point = [&](double x, double y) -> Double2 {
            return {tx + std::cos(angle) * x - std::sin(angle) * sign * y,
                    ty + std::sin(angle) * x + std::cos(angle) * sign * y};
        };
        const auto ray_origin = point(1.5, 2), ray_end = point(1.5, -2);
        const Double2 displacement{ray_end[0] - ray_origin[0], ray_end[1] - ray_origin[1]};
        const auto hits = physics->raycast_all_2d(ray_origin, displacement);
        PhysicsOverlap2D overlap;
        overlap.origin = point(1.5, -.5);
        overlap.dimensions = {.1, 0};
        PhysicsSweep2D sweep;
        sweep.origin = ray_origin;
        sweep.displacement = displacement;
        sweep.dimensions = {.1, 0};
        const auto swept = physics->shape_cast_2d(sweep);
        check(hits.size() == 1 && hits[0].entity == floor &&
                  std::abs(hits[0].fraction - .5) < 1e-5 &&
                  physics->overlap_shape_2d(overlap) == std::vector<EntityRef>{floor} && swept &&
                  swept->entity == floor,
              "Moving/trigger Map queries lost rotated compound geometry or unique identity");
        const PhysicsQueryFilter no_sensors{UINT32_MAX, false};
        check(bool(physics->raycast_2d(ray_origin, displacement, no_sensors)) == !sensor &&
                  physics->overlap_shape_2d(overlap, no_sensors).empty() == sensor &&
                  bool(physics->shape_cast_2d(sweep, no_sensors)) == !sensor,
              "Map sensor policy disagreed across ray/overlap/sweep queries");
        const std::array<EntityRef, 1> ignored{floor};
        const PhysicsQueryFilter excluded{UINT32_MAX, true, ignored};
        check(physics->raycast_all_2d(ray_origin, displacement, excluded).empty() &&
                  physics->overlap_shape_2d(overlap, excluded).empty() &&
                  !physics->shape_cast_2d(sweep, excluded),
              "Canonical exclusion retained a moving/trigger Map rectangle");
        physics->teleport_2d(actor, point(.1, -.5), 0, true);
        world.simulation.tick(1.f / 60);
        if (sensor || one_way) {
            check(!sensor || event(true, true, epoch), "Map sensor did not report reentry");
            std::vector<TileMapCellEdit> erase, fill;
            for (int x = -3; x < 5; ++x) {
                erase.push_back({1, {x, -1}, {}});
                fill.push_back({1, {x, -1}, {2, 0}});
            }
            const auto edit = request_runtime_tile_cells(context, "project.moving_maps", floor,
                                                         cells->generation, erase);
            world.simulation.tick(1.f / 60);
            check(inspect_runtime_tile_edit(context, "project.moving_maps", edit).state ==
                          RuntimeTileEditState::Applied &&
                      !physics->body_state_2d(floor) && (!sensor || event(false, false, epoch)),
                  "Erased Map sensor retained collision or misbound retired End");
            release_runtime_tile_edit(context, "project.moving_maps", edit);
            const auto refill = request_runtime_tile_cells(context, "project.moving_maps", floor,
                                                           cells->generation, fill);
            world.simulation.tick(1.f / 60);
            const auto next = physics->body_state_2d(floor);
            check(next && next->epoch > epoch && (!sensor || event(true, true, next->epoch)),
                  "Refilled Map sensor did not realize a fresh epoch/Begin");
            release_runtime_tile_edit(context, "project.moving_maps", refill);
        }
        const auto snapshot = world.scene.snapshot(), saved_physics = physics->checkpoint();
        const auto saved_maps = checkpoint_runtime_tile_maps(context);
        RuntimeWorld recovered({}, {}, std::nullopt, root, false);
        recovered.scene.restore_snapshot(snapshot);
        // Restore into an unpublished native world: preparing Physics here
        // would realize authored geometry before edited cells are restored.
        const auto deadline = std::chrono::steady_clock::now() + 15s;
        for (;;) {
            recovered.engine.services().resources()->synchronize();
            synchronize_runtime_tile_maps(recovered.engine.world());
            if (prepare_runtime_tile_maps(recovered.engine.world()))
                break;
            check(std::chrono::steady_clock::now() < deadline,
                  "Moving/trigger Map recovery resources stalled");
            std::this_thread::sleep_for(1ms);
        }
        restore_runtime_tile_maps(recovered.engine.world(), saved_maps);
        recovered.physics()->restore(saved_physics);
        recovered.simulation.restore_input_tick(physics->status().at("tick").get<std::uint64_t>());
        recovered.simulation.reset_presentation();
        check(!recovered.physics()->contact_count_2d() &&
                  recovered.physics()->body_state_2d(floor) == physics->body_state_2d(floor),
              "Fresh moving/trigger Map recovery lost state or replayed events");
        for (unsigned i = 0; i < 5; ++i) {
            world.simulation.tick(1.f / 60);
            recovered.simulation.tick(1.f / 60);
            check(recovered.physics()->body_state_2d(floor) == physics->body_state_2d(floor) &&
                      recovered.physics()->body_state_2d(actor) == physics->body_state_2d(actor),
                  "Fresh moving/trigger Map recovery diverged during continuation");
        }
    }
}
