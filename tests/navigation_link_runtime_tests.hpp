#pragma once
// Called by the authored-link pipeline with normally published Nav assets.
void movement(const Json& snapshot, const std::filesystem::path& root, EntityId actor_id,
              const AssetRecord& nav, const AssetRecord& other, EntityId source, Double3 start,
              NavigationPlane plane) {
    for (bool avoiding : {false, true}) {
        EngineContext engine(WorldRole::Runtime, false, {navigation_module(root)});
        Scene scene(engine.world());
        scene.restore_snapshot(snapshot);
        RuntimeSimulation simulation(engine.world(), scene);
        const auto actor = scene.entity(actor_id.str());
        if (avoiding)
            actor.set<NavigationAvoidance>({});
        auto navigation =
            std::static_pointer_cast<NavigationRuntime>(engine.world().services().navigation());
        const auto goal = actor.get<NavigationAgent>();
        check(navigation->project_point({other.id}, start).status == NavStatus::Success &&
                  navigation
                          ->find_path({nav.id}, start,
                                      {goal.destination_x, goal.destination_y, goal.destination_z})
                          .status == NavStatus::Success,
              "Runtime did not select link provenance independently from the unlinked profile");
        bool crossed = false, paused = false, exited = false;
        for (unsigned tick = 0; tick != 360; ++tick) {
            const auto previous = actor.get<LocalTranslation>();
            simulation.tick(1.f / 60);
            auto position = actor.get<LocalTranslation>();
            auto config = actor.get<NavigationAgent>();
            check(std::hypot(position.x - previous.x, position.y - previous.y,
                             position.z - previous.z) <= double(config.speed) / 60 + .00001,
                  "Agent link crossing exceeded the fixed-tick world-distance speed budget");
            if (plane == NavigationPlane::PlanarXY)
                check(position.z == start[2], "XY crossing changed the actor's visual depth");
            const auto debug = navigation->debug(actor.id());
            if (position.x > -.75 && position.x < .75) {
                crossed = true;
                check(!debug.at("link").is_null() &&
                          debug.at("link").at("source").get<EntityId>() == source,
                      "Agent in the gap did not retain its explicit link source");
                if (!paused) {
                    paused = true;
                    const auto checkpoint = navigation->checkpoint();
                    check(checkpoint.at("version") == 4 &&
                              checkpoint.at("agents").at(0).contains("link"),
                          "Active traversal was not captured in additive recovery version 4");
                    const auto saved = scene.snapshot();
                    {
                        EngineContext recovered(WorldRole::Runtime, false,
                                                {navigation_module(root)});
                        Scene restored(recovered.world());
                        restored.restore_snapshot(saved);
                        RuntimeSimulation resumed(recovered.world(), restored);
                        auto service = std::static_pointer_cast<NavigationRuntime>(
                            recovered.world().services().navigation());
                        service->restore(checkpoint);
                        for (unsigned step = 0; step != 2; ++step) {
                            resumed.tick(1.f / 60);
                            simulation.tick(1.f / 60);
                            const auto a = actor.get<LocalTranslation>(),
                                       b = restored.entity(actor_id.str()).get<LocalTranslation>();
                            check(std::hypot(a.x - b.x, a.y - b.y, a.z - b.z) < .000001,
                                  "Recovered traversal did not continue from the same fixed-tick "
                                  "position");
                        }
                    }
                    if (!avoiding) {
                        for (unsigned damage = 0; damage != 8; ++damage) {
                            auto invalid = checkpoint;
                            auto& row = invalid["agents"][0];
                            if (damage == 0)
                                invalid["version"] = 3;
                            if (damage == 1)
                                row["link"]["source"] = EntityId::generate();
                            if (damage == 2)
                                row["link"]["entry"][0] =
                                    row["link"]["entry"][0].get<double>() + .1;
                            if (damage == 3)
                                std::swap(row["link"]["entry"], row["link"]["exit"]);
                            if (damage == 4)
                                row["translation"][1] = row["translation"][1].get<double>() + .2;
                            if (damage == 5)
                                invalid["assets"][0]["sha256"] = std::string(64, '0');
                            if (damage == 6)
                                row["link"]["entry"].push_back(0);
                            if (damage == 7)
                                row["link"]["exit"].erase(2);
                            EngineContext rejected(WorldRole::Runtime, false,
                                                   {navigation_module(root)});
                            Scene candidate(rejected.world());
                            candidate.restore_snapshot(saved);
                            RuntimeSimulation candidate_simulation(rejected.world(), candidate);
                            auto service = std::static_pointer_cast<NavigationRuntime>(
                                rejected.world().services().navigation());
                            bool threw = false;
                            try {
                                service->restore(invalid);
                            } catch (const std::exception&) {
                                threw = true;
                            }
                            check(threw, "Malformed traversal recovery reached a published "
                                         "movement continuation");
                        }
                    }
                    position = actor.get<LocalTranslation>();
                    const auto retained = position;
                    for (unsigned mode = 0; mode != 3; ++mode) {
                        auto held = config;
                        if (mode == 0)
                            held.speed = 0;
                        if (mode == 1)
                            held.enabled = false;
                        if (mode == 2)
                            held.has_destination = false;
                        actor.set<NavigationAgent>(held);
                        for (unsigned wait = 0; wait != 3; ++wait)
                            simulation.tick(1.f / 60);
                        check(actor.get<LocalTranslation>() == retained &&
                                  !navigation->debug(actor.id()).at("link").is_null(),
                              "Pause, disable or cleared destination lost traversal or moved the "
                              "actor");
                    }
                    config.speed = 2;
                    config.stopping_distance = 5;
                    if (plane == NavigationPlane::PlanarXY)
                        config.destination_y = 1;
                    else
                        config.destination_z = 1;
                    actor.set<NavigationAgent>(config);
                }
            }
            if (paused && !exited && debug.at("link").is_null()) {
                exited = true;
                check(position.x >= 1.9,
                      "Retarget or stopping distance ended a traversal inside the gap");
                config.stopping_distance = .05f;
                actor.set<NavigationAgent>(config);
            }
            if (exited && navigation->agent_state(scene.reference(actor_id.str()))->phase ==
                              NavigationAgentPhase::Arrived)
                break;
        }
        const auto observation = navigation->agent_state(scene.reference(actor_id.str()));
        check(crossed && paused && exited && observation->phase == NavigationAgentPhase::Arrived &&
                  actor.get<LocalTranslation>().x > 2.8,
              "Agent did not complete the link and resume the retargeted ground route: plane=" +
                  std::to_string(unsigned(plane)) + " avoidance=" + std::to_string(avoiding) +
                  " crossed/paused/exited=" + std::to_string(crossed) + std::to_string(paused) +
                  std::to_string(exited) + " " + navigation->debug(actor.id()).dump());
        check(plane != NavigationPlane::Spatial3D || actor.get<LocalTranslation>().y > 1.8,
              "Spatial traversal did not ascend to the elevated destination floor");
        auto changed = scene.entity(source.str()).get<NavigationLink>();
        const auto original = changed;
        changed.end_x += .1f;
        scene.entity(source.str()).set<NavigationLink>(changed);
        check(navigation->project_point({nav.id}, start).status == NavStatus::Stale &&
                  navigation->project_point({other.id}, start).status == NavStatus::Success,
              "Runtime link edit did not stale only its own selected profile");
        scene.entity(source.str()).set<NavigationLink>(original);
        check(navigation->project_point({nav.id}, start).status == NavStatus::Success,
              "Restoring exact source values did not restore current navigation selection");
    }
}

void link_lifecycle(const Json& snapshot, const std::filesystem::path& root, EntityId actor_id,
                    const AssetRecord& nav, const AssetRecord& other, EntityId source,
                    EntityId obstacle_id, Double3 start, bool dynamic) {
    for (unsigned mode = dynamic ? 0 : 1; mode != 5; ++mode) {
        EngineContext engine(WorldRole::Runtime, false, {navigation_module(root)});
        Scene scene(engine.world());
        scene.restore_snapshot(snapshot);
        RuntimeSimulation simulation(engine.world(), scene);
        auto navigation =
            std::static_pointer_cast<NavigationRuntime>(engine.world().services().navigation());
        const auto actor = scene.entity(actor_id.str());
        for (unsigned step = 0; step != 180 && actor.get<LocalTranslation>().x < -.7; ++step)
            simulation.tick(1.f / 60);
        const auto held = actor.get<LocalTranslation>();
        check(held.x > -.75 && held.x < .75 && !navigation->debug(actor.id()).at("link").is_null(),
              "Lifecycle fixture did not begin an actual gap traversal");
        auto config = actor.get<NavigationAgent>();
        if (mode == 0) {
            const auto obstacle = scene.entity(obstacle_id.str());
            auto value = obstacle.get<NavigationObstacle>();
            value.enabled = true;
            obstacle.set<NavigationObstacle>(value);
            for (unsigned wait = 0; wait != 3; ++wait)
                simulation.tick(1.f / 60);
            check(actor.get<LocalTranslation>() == held &&
                      navigation->agent_state(scene.reference(actor_id.str()))->phase ==
                          NavigationAgentPhase::Blocked &&
                      !navigation->debug(actor.id()).at("link").is_null(),
                  "A carved link endpoint did not hold its in-flight traversal");
            value.enabled = false;
            obstacle.set<NavigationObstacle>(value);
        } else if (mode == 1) {
            auto value = scene.entity(source.str()).get<NavigationLink>();
            const auto original = value;
            value.end_x += .1f;
            scene.entity(source.str()).set<NavigationLink>(value);
            simulation.tick(1.f / 60);
            const auto observation = navigation->agent_state(scene.reference(actor_id.str()));
            check(actor.get<LocalTranslation>() == held &&
                      observation->phase == NavigationAgentPhase::Failed &&
                      observation->status == NavStatus::Stale &&
                      !navigation->debug(actor.id()).at("link").is_null(),
                  "Stale source continued movement or lost its held traversal");
            scene.entity(source.str()).set<NavigationLink>(original);
        } else if (mode == 2) {
            actor.set<LocalTranslation>({start[0], start[1], start[2]});
            simulation.tick(1.f / 60);
            check(actor.get<LocalTranslation>().x < -2.5 &&
                      actor.get<LocalTranslation>().x > start[0] &&
                      navigation->debug(actor.id()).at("link").is_null(),
                  "Direct repositioning reused the old in-flight span");
        } else if (mode == 3) {
            config.navmesh = {other.id};
            actor.set<NavigationAgent>(config);
            simulation.tick(1.f / 60);
            check(actor.get<LocalTranslation>() == held &&
                      navigation->debug(actor.id()).at("link").is_null() &&
                      navigation->agent_state(scene.reference(actor_id.str()))->phase ==
                          NavigationAgentPhase::Failed,
                  "Changing the selected asset reused a crossing admitted by another asset");
            config.navmesh = {nav.id};
            actor.set<NavigationAgent>(config).set<LocalTranslation>(
                {start[0], start[1], start[2]});
        } else {
            actor.remove<NavigationAgent>();
            simulation.tick(1.f / 60);
            check(!navigation->agent_state(scene.reference(actor_id.str())),
                  "Removed Agent retained a runtime observation");
            actor.set<NavigationAgent>(config).set<LocalTranslation>(
                {start[0], start[1], start[2]});
        }
        for (unsigned step = 0; step != 240; ++step)
            simulation.tick(1.f / 60);
        check(navigation->agent_state(scene.reference(actor_id.str()))->phase ==
                      NavigationAgentPhase::Arrived &&
                  actor.get<LocalTranslation>().x > 2.8,
              "Traversal lifecycle did not resume/restart and arrive: mode=" +
                  std::to_string(mode) + " " + navigation->debug(actor.id()).dump());
    }
}
