#pragma once
#include "navigation_obstacle.hpp"

void dynamic_obstacle_runtime_tests(const std::filesystem::path& parent,
                                    const std::filesystem::path& worker) {
    using namespace forge::navigation_detail;
    const auto identity = EntityId::generate();
    NavigationSettings shape_profile;
    const auto mirrored = obstacle_cylinder(
        identity, {true, .5f, 1},
        affine_transform({{0, 1, 0}, rotation_from_euler({0, 90, 0}), {2, 3, -2}}), shape_profile);
    check(std::abs(mirrored.radius - 1.4f) < .00001 && mirrored.height == 3 &&
              mirrored.position[1] == -.5f,
          "Upright mirrored/yawed cylinder lost world dimensions");
    reject([&] {
        obstacle_cylinder(identity, {true, .5f, 1},
                          affine_transform({{}, rotation_from_euler({30, 0, 0}), {1, 1, 1}}),
                          shape_profile);
    });
    shape_profile.plane = NavigationPlane::PlanarXY;
    auto transformed =
        affine_transform({{-3, 4, 100}, rotation_from_euler({0, 0, 35}), {-2, 2, 0}});
    const auto xy_circle = obstacle_cylinder(identity, {true, .5f, 1}, transformed, shape_profile);
    transformed.m[11] = -100;
    check(xy_circle == obstacle_cylinder(identity, {true, .5f, 1}, transformed, shape_profile) &&
              std::abs(xy_circle.radius - 1.4f) < .00001 && xy_circle.position[0] == -3 &&
              xy_circle.position[2] == 4,
          "Mirrored/rotated XY circle depends on visual depth");
    for (const auto plane : {NavigationPlane::Spatial3D, NavigationPlane::PlanarXY}) {
        const auto root =
            parent / (plane == NavigationPlane::Spatial3D ? "dynamic3d" : "dynamicxy");
        std::filesystem::create_directories(root);
        EngineContext author;
        Scene authored(author.world());
        authored.restore_snapshot(input());
        auto floor = authored.entity("floor"), blocker = authored.entity("obstacle"),
             agent = authored.entity("agent");
        if (plane == NavigationPlane::PlanarXY) {
            floor.set<LocalScale>({1, 1, 0});
            floor.set<NavigationRegion2D>({true, false, 20, 20});
            blocker.set<LocalTranslation>({0, 0, 19});
            agent.set<LocalTranslation>({-8, 0, 37.125});
        } else {
            floor.set<NavigationSurface>({true});
            blocker.set<LocalTranslation>({0, 1, 0});
        }
        blocker.set<LocalScale>({1, 1, 1}).set<NavigationObstacle>({false, 1.5f, 2});
        NavigationSettings settings;
        settings.plane = plane;
        settings.dynamic_obstacles = true;
        const auto source_digest = geometry(capture(authored.effective_document(), plane)).digest;
        auto candidate = prepare_navigation(root, authored.effective_document(), settings, worker);
        // Obstacle edits do not change the static sources or invalidate a bake candidate.
        blocker.set<NavigationObstacle>({true, 1.5f, 2});
        const auto record = candidate.publish(authored.effective_document());
        check(
            record.schema_version == 4 &&
                AssetCatalog::open_project(root).resolve(AssetRef<NavMeshAsset>{record.id}).state ==
                    AssetState::Available &&
                geometry(capture(authored.effective_document(), plane)).digest == source_digest,
            "Dynamic profile publication/catalog admission changed static source identity");
        blocker.set<NavigationObstacle>({false, 1.5f, 2});
        agent.set<NavigationAgent>(
            {{record.id}, true, true, 0, .1f, 8, plane == NavigationPlane::PlanarXY ? 0. : .1, 0});
        const auto snapshot = authored.snapshot();
        // A C++ controller may query from a read-only fixed callback without
        // using NavigationAgent as its transform writer.
        {
            Fixture controller(root);
            controller.scene.restore_snapshot(snapshot);
            controller.scene.entity("agent").remove<NavigationAgent>();
            controller.scene.entity("obstacle").set<NavigationObstacle>({true, 1.5f, 2});
            const Double3 start =
                plane == NavigationPlane::PlanarXY ? Double3{-8, 0, 37.125} : Double3{-8, .1, 0};
            const Double3 goal =
                plane == NavigationPlane::PlanarXY ? Double3{8, 0, 7} : Double3{8, .1, 0};
            const auto unchanged_position = controller.position();
            controller.navigation->synchronize();
            const auto query = [&] {
                ecs_readonly_begin(controller.engine.world().world().c_ptr(), false);
                const auto route = controller.navigation->find_path({record.id}, start, goal);
                ecs_readonly_end(controller.engine.world().world().c_ptr());
                return route;
            };
            check(query().status == NavStatus::Unavailable,
                  "First read-only dynamic query bypassed writable-boundary admission");
            controller.tick();
            const auto carved = query();
            check(carved.status == NavStatus::Success && carved.points.size() > 2,
                  "Standalone C++ query never adopted obstacles without a NavigationAgent");
            controller.scene.entity("obstacle").set<NavigationObstacle>({false, 1.5f, 2});
            controller.tick();
            check(query().points.size() == 2,
                  "Standalone controller failed to restore tiles after disabling an obstacle");
            check(controller.position() == unchanged_position,
                  "Standalone controller query acquired a transform writer");
        }
        {
            Fixture failed(root);
            failed.scene.restore_snapshot(snapshot);
            failed.scene.entity("obstacle").set<NavigationObstacle>({true, 4, 2});
            const auto extra =
                authoring_command(failed.scene, "entity.create", {{"name", "Second agent"}})
                    .at("selected")
                    .get<std::string>();
            failed.scene.entity(extra)
                .set<LocalTranslation>(plane == NavigationPlane::PlanarXY
                                           ? LocalTranslation{-8, 0, 37.125}
                                           : LocalTranslation{-8, .1, 0})
                .set<NavigationAgent>(failed.scene.entity("agent").get<NavigationAgent>());
            failed.engine.services().profiling(true);
            failed.tick();
#ifndef FORGE_DISABLE_PROFILING
            unsigned captures = 0, attempts = 0;
            for (const auto& sample : failed.engine.services().profiles()) {
                captures += sample.at("name") == "NavSourceCapture";
                attempts += sample.at("name") == "NavObstacleUpdate";
            }
            check(attempts > 0 && attempts <= captures,
                  "Failed obstacle snapshot rebuilt once per agent instead of once per boundary");
#endif
            const auto ref =
                failed.engine.world().reference(failed.scene.entity(extra).id()).value();
            check(failed.navigation->agent_state(ref)->phase == NavigationAgentPhase::Failed,
                  "Failed snapshot allowed another agent to use stale navigation");
            failed.scene.entity("obstacle").set<NavigationObstacle>({false, 4, 2});
            failed.tick();
            check(failed.navigation->agent_state(ref)->status == NavStatus::Success,
                  "Valid snapshot failed to retire a remembered update error");
        }
        {
            EngineContext prefab_author;
            Scene prefabs(prefab_author.world());
            prefabs.restore_snapshot(snapshot);
            const auto prefab = create_prefab_source(prefabs, "obstacle");
            prefabs.set_prefab_sources({{prefab.asset(), prefab.source}});
            const auto instance = instantiate_prefab(prefabs, prefab.asset());
            check(!prefabs.entity(instance).owns<NavigationObstacle>(),
                  "Navigation obstacle did not inherit its prefab data");
            authoring_command(prefabs, "property.set",
                              {{"entity", instance},
                               {"component", "forge.navigation_obstacle"},
                               {"field", "radius"},
                               {"value", 1.25}});
            const auto prefab_snapshot = prefabs.snapshot();
            Fixture inherited(root);
            inherited.scene.restore_snapshot(prefab_snapshot);
            auto participant = inherited.scene.entity(instance);
            auto config = participant.get<NavigationObstacle>();
            config.enabled = true;
            config.height = 2.5f;
            participant.set<NavigationObstacle>(config);
            inherited.tick();
            check(inherited.navigation->debug(inherited.scene.entity("agent").id())
                          .at("path")
                          .size() > 2,
                  "Instanced prefab obstacle was omitted from the Scene's carved route");
            const auto checkpoint = inherited.navigation->checkpoint();
            Fixture cold(root);
            cold.scene.restore_snapshot(inherited.scene.snapshot());
            check(cold.scene.entity(instance).get<NavigationObstacle>() == config,
                  "Cold prefab snapshot lost native obstacle values outside its authored mask");
            cold.navigation->restore(checkpoint);
            cold.tick();
            check(cold.navigation->debug(cold.scene.entity("agent").id()).at("path") ==
                          inherited.navigation->debug(inherited.scene.entity("agent").id())
                              .at("path") &&
                      prefabs.snapshot() == prefab_snapshot,
                  "Prefab obstacle recovery changed the route or authored Scene");
        }
        Fixture run(root), independent(root);
        run.scene.restore_snapshot(snapshot);
        independent.scene.restore_snapshot(snapshot);
        auto live_blocker = run.scene.entity("obstacle");
        const auto live_agent = run.scene.entity("agent");
        const auto reference = run.engine.world().reference(live_agent.id()).value();
        run.tick();
        independent.tick();
        const auto direct = run.navigation->debug(live_agent.id());
        check(direct.at("path").size() == 2 && direct.at("phase") == "Paused",
              "Dynamic base agent did not acquire its unchanged direct route");
        live_blocker.set<NavigationObstacle>({true, 1.5f, 2});
        run.tick();
        const auto carved = run.navigation->debug(live_agent.id());
        check(carved.at("path").size() > 2 &&
                  carved.at("remaining_distance").get<double>() >
                      direct.at("remaining_distance").get<double>() + .1 &&
                  independent.navigation->debug(independent.scene.entity("agent").id())
                          .at("path")
                          .size() == 2,
              "A stationary nonavoiding agent failed to replan, or another World was changed");
        live_blocker.set<LocalTranslation>(plane == NavigationPlane::PlanarXY
                                               ? LocalTranslation{0, 6, -123}
                                               : LocalTranslation{0, 1, 6});
        run.tick();
        check(run.navigation->debug(live_agent.id()).at("path").size() == 2,
              "Moving the obstacle left the agent following an old detour");
        live_blocker.set<LocalScale>(LocalScale{2, 1, 1});
        const auto before_invalid = run.position();
        run.tick();
        check(run.navigation->agent_state(reference)->phase == NavigationAgentPhase::Failed &&
                  run.position() == before_invalid,
              "Unsupported elliptical obstacle allowed stale-route movement");
        live_blocker.set<LocalScale>({1, 1, 1});
        live_blocker.set<LocalTranslation>(plane == NavigationPlane::PlanarXY
                                               ? LocalTranslation{0, 0, 19}
                                               : LocalTranslation{0, 1, 0});
        run.navigation->synchronize();
        ecs_readonly_begin(run.engine.world().world().c_ptr(), false);
        const auto deferred = run.navigation->find_path(
            {record.id}, run.position(),
            plane == NavigationPlane::PlanarXY ? Double3{8, 0, 7} : Double3{8, .1, 0});
        ecs_readonly_end(run.engine.world().world().c_ptr());
        check(deferred.status == NavStatus::Success && deferred.points.size() > 2,
              "Writable synchronization did not prepare a read-only carved query");
        run.tick();
        check(run.navigation->debug(live_agent.id()).at("path").size() > 2,
              "Writable boundary did not apply a deferred obstacle update");
        const auto recovery = run.navigation->checkpoint();
        Fixture recovered(root);
        recovered.scene.restore_snapshot(run.scene.snapshot());
        recovered.navigation->restore(recovery);
        recovered.tick();
        check(recovered.navigation->debug(recovered.scene.entity("agent").id()).at("path") ==
                  run.navigation->debug(live_agent.id()).at("path"),
              "Recovery failed to reconstruct routes from current obstacle poses");
        auto moving = live_agent.get<NavigationAgent>();
        moving.speed = 3;
        live_agent.set<NavigationAgent>(moving);
        bool detoured = false;
        for (unsigned tick = 0; tick < 440; ++tick) {
            const auto before = run.position();
            run.tick();
            const auto position = run.position();
            check(std::hypot(position[0] - before[0], position[1] - before[1],
                             position[2] - before[2]) <= 3. / 60 + .00001 &&
                      run.navigation->agent_state(reference)->status == NavStatus::Success,
                  "Carved agent route exceeded its movement budget or failed");
            detoured |= std::abs(position[plane == NavigationPlane::PlanarXY ? 1 : 2]) > 1.8;
            if (plane == NavigationPlane::PlanarXY)
                check(position[2] == 37.125, "Carved XY movement changed visual depth");
        }
        check(detoured &&
                  run.navigation->agent_state(reference)->phase == NavigationAgentPhase::Arrived,
              "Agent failed to follow the carved route and arrive");
        live_blocker.remove<NavigationObstacle>();
        check(run.navigation->find_path({record.id}, {-8, .1, 0}, {8, .1, 0}).points.size() == 2,
              "Deleting the component did not restore base navigation");
        // The same shape API rejects malformed direct C++ data, independent of UI validation.
        reject([&] { obstacle_cylinder(EntityId::generate(), {true, 0, 2}, {}, settings); });
        std::cout << "Dynamic obstacle worker/runtime/generation/World/recovery/movement "
                  << (plane == NavigationPlane::PlanarXY ? "XY" : "3D") << " PASS\n";
    }
}
