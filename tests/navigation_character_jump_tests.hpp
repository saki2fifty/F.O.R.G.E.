#pragma once

void character_jumps(const Json& source, const std::filesystem::path& root, EntityId actor_id) {
    for (unsigned scenario = 0; scenario != 13; ++scenario)
        for (bool avoiding : {false, true}) {
            const bool blocked = scenario == 1;
            EngineContext engine(WorldRole::Runtime, false,
                                 {navigation_module(root), physics_module({}, root)});
            Scene scene(engine.world());
            scene.restore_snapshot(source);
            RuntimeSimulation simulation(engine.world(), scene);
            const auto navigation =
                std::static_pointer_cast<NavigationRuntime>(engine.services().navigation());
            const auto physics =
                std::static_pointer_cast<PhysicsRuntime>(engine.services().physics());
            const auto actor = scene.entity(actor_id.str());
            const auto ref = engine.world().reference(actor.id()).value();
            actor.set<SpatialBinding>({SpatialMode::World, {}}).set<CharacterController>({});
            if (avoiding)
                actor.set<NavigationAvoidance>({});
            auto config = actor.get<NavigationAgent>();
            config.movement = 1;
            config.speed = 6;
            config.enabled = false;
            if (scenario == 12) {
                config.stopping_distance = 5;
                config.destination_x = 2;
                actor.set<LocalTranslation>({-2, .1, 0});
            }
            actor.set<NavigationAgent>(config);
            const auto solid = [&](const char* name, LocalTranslation position, BoxCollider box) {
                const auto id = authoring_command(scene, "entity.create", {{"name", name}})
                                    .at("selected")
                                    .get<std::string>();
                scene.entity(id)
                    .set<LocalTranslation>(position)
                    .set<SpatialBinding>({SpatialMode::World, {}})
                    .set<PhysicsBody>({})
                    .set<BoxCollider>(box);
                return scene.entity(id);
            };
            const auto launch = solid("Takeoff support", {-3, -.5, 0}, {4, 1, 6});
            const auto landing = solid("Landing support", {3, 1.5, 0}, {4, 1, 6});
            if (blocked)
                solid("Physical flight obstruction", {0, 3, 0}, {.4, 20, 6});
            if (scenario == 6) {
                auto settings = PhysicsConfig{};
                settings.gravity = {1, -9.81, 0};
                physics->configure(settings);
            }
            for (unsigned i = 0; i != 10; ++i)
                simulation.tick(1.f / 60);
            if (scenario == 5) {
                auto body = landing.get<PhysicsBody>();
                body.enabled = false;
                landing.set<PhysicsBody>(body);
                physics->synchronize(0);
            }
            if (scenario == 7)
                config.speed = 2;
            if (scenario == 8) {
                auto body = launch.get<PhysicsBody>();
                body.motion = 1;
                launch.set<PhysicsBody>(body);
                physics->synchronize(0);
            }
            config.enabled = true;
            actor.set<NavigationAgent>(config);
            bool took_off = false, airborne = false, recovered = false, arrived = false,
                 exercised = false, refused = false;
            double max_x = -100;
            for (unsigned i = 0; i != 240; ++i) {
                simulation.tick(1.f / 60);
                const auto actual = physics->character(ref);
                const auto debug = navigation->debug(actor.id());
                refused |= debug.at("phase") == "Failed";
                took_off |= actual.jump_accepted;
                airborne |= actual.position.x > -1 && actual.position.y > 1;
                max_x = std::max(max_x, actual.position.x);
                if (!exercised && actual.position.x > -.5 && actual.position.x < .5) {
                    exercised = true;
                    if (scenario == 2) {
                        config.speed = 0;
                        actor.set<NavigationAgent>(config);
                        simulation.tick(1.f / 60);
                        const auto paused = physics->character(ref);
                        check(std::abs(paused.position.x - actual.position.x) < 1e-6 &&
                                  std::abs(paused.position.y - actual.position.y) > 1e-5 &&
                                  navigation->debug(actor.id()).at("phase") == "Paused",
                              "Zero navigation speed froze gravity or retained horizontal intent");
                        config.speed = 6;
                        actor.set<NavigationAgent>(config);
                    }
                    if (scenario == 3) {
                        config.has_destination = false;
                        actor.set<NavigationAgent>(config);
                        for (unsigned step = 0; step != 5; ++step)
                            simulation.tick(1.f / 60);
                        const auto cancelled = physics->character(ref);
                        check(std::abs(cancelled.position.x - actual.position.x) < 1e-6 &&
                                  std::abs(cancelled.position.y - actual.position.y) > 1e-3 &&
                                  navigation->debug(actor.id()).at("phase") == "Idle",
                              "Cancelling navigation retained steering or stopped physical flight");
                        break;
                    }
                    if (scenario == 10) {
                        scene.world().each([](flecs::entity e, const NavigationLink& value) {
                            auto changed = value;
                            changed.enabled = false;
                            e.set<NavigationLink>(changed);
                        });
                        for (unsigned step = 0; step != 5; ++step)
                            simulation.tick(1.f / 60);
                        const auto released = physics->character(ref);
                        const auto failed = navigation->debug(actor.id());
                        check(std::abs(released.position.x - actual.position.x) < 1e-6 &&
                                  std::abs(released.position.y - actual.position.y) > 1e-3 &&
                                  failed.at("phase") == "Failed" &&
                                  navigation->agent_state(ref)->status == NavStatus::Stale,
                              "Stale flight source retained steering or suspended native gravity");
                        break;
                    }
                    if (scenario == 11) {
                        physics->move_character(ref, {0, 0, 1});
                        simulation.tick(1.f / 60);
                        const auto manual = physics->character(ref);
                        check(std::abs(manual.position.x - actual.position.x) < 1e-6 &&
                                  manual.position.z > actual.position.z + .01 &&
                                  manual.velocity[2] > .5,
                              "Flight steering overrode a queued manual movement request");
                        physics->move_character(ref, {});
                    }
                    if (scenario == 4) {
                        config.destination_x = 3.5;
                        actor.set<NavigationAgent>(config);
                    }
                    if (scenario == 9) {
                        auto controller = actor.get<CharacterController>();
                        controller.max_slope = 40;
                        actor.set<CharacterController>(controller);
                        physics->synchronize(0);
                    }
                }
                if (debug.at("sampled").get<bool>())
                    check(debug.at("position").get<Double3>() == Double3{actual.position.x,
                                                                         actual.position.y,
                                                                         actual.position.z} &&
                              debug.at("velocity").get<Double3>() == actual.velocity,
                          "Jump navigation published a prescribed flight instead of physics");
                if (!blocked && !recovered && actual.position.x > -.5 && actual.position.x < .5) {
                    recovered = true;
                    const auto saved_scene = scene.snapshot();
                    const auto saved_physics = physics->checkpoint();
                    const auto saved_navigation = navigation->checkpoint();
                    check(
                        saved_navigation.at("version") == 9 &&
                            saved_navigation.at("agents").at(0).at("translation").is_null() &&
                            saved_navigation.at("agents").at(0).at("link").at("physical_jump") ==
                                true,
                        "Physical jump recovery duplicated pose authority or omitted its profile");
                    EngineContext restored(WorldRole::Runtime, false,
                                           {navigation_module(root), physics_module({}, root)});
                    Scene restored_scene(restored.world());
                    restored_scene.restore_snapshot(saved_scene);
                    RuntimeSimulation resumed(restored.world(), restored_scene);
                    const auto restored_physics =
                        std::static_pointer_cast<PhysicsRuntime>(restored.services().physics());
                    const auto restored_nav = std::static_pointer_cast<NavigationRuntime>(
                        restored.services().navigation());
                    restored_physics->restore(saved_physics);
                    restored_nav->restore(saved_navigation);
                    for (unsigned step = 0; step != 3; ++step) {
                        resumed.tick(1.f / 60);
                        simulation.tick(1.f / 60);
                        const auto a = physics->character(ref),
                                   b = restored_physics->character(ref);
                        check(std::hypot(a.position.x - b.position.x, a.position.y - b.position.y,
                                         a.position.z - b.position.z) < 1e-6,
                              "Recovered physical jump diverged from the native character");
                    }
                    if (!avoiding)
                        for (unsigned bad = 0; bad != 8; ++bad) {
                            auto damaged = saved_navigation;
                            auto& row = damaged["agents"].at(0);
                            if (bad == 0)
                                damaged["version"] = 8;
                            if (bad == 1)
                                row["link"]["physical_jump"] = false;
                            if (bad == 2)
                                row["link"]["physical_jump"] = "true";
                            if (bad == 3)
                                row["link"].erase("physical_jump");
                            if (bad == 4)
                                row["link"]["source"] = EntityId::generate();
                            if (bad == 5)
                                row["link"]["exit"][0] = 20;
                            if (bad == 6)
                                row["translation"] = Json::array({0, 0, 0});
                            if (bad == 7)
                                row["link"]["arc_height"] = 0;
                            EngineContext candidate(
                                WorldRole::Runtime, false,
                                {navigation_module(root), physics_module({}, root)});
                            Scene candidate_scene(candidate.world());
                            candidate_scene.restore_snapshot(saved_scene);
                            const auto candidate_physics = std::static_pointer_cast<PhysicsRuntime>(
                                candidate.services().physics());
                            const auto candidate_nav = std::static_pointer_cast<NavigationRuntime>(
                                candidate.services().navigation());
                            candidate_physics->restore(saved_physics);
                            bool rejected = false;
                            try {
                                candidate_nav->restore(damaged);
                            } catch (const std::exception&) {
                                rejected = true;
                            }
                            check(rejected, "Malformed physical jump recovery was admitted");
                        }
                }
                if (debug.at("phase") == "Arrived") {
                    if (scenario == 12) {
                        check(!took_off && std::abs(actual.position.x + 2) < 1e-6 &&
                                  std::abs(actual.position.y) < .1 &&
                                  debug.at("remaining_distance").get<double>() <= 5,
                              "Character jumped despite already satisfying its stopping distance");
                        arrived = true;
                        break;
                    }
                    check(!blocked && actual.ground == CharacterGround::OnGround &&
                              std::abs(actual.position.y - 2) < .1 &&
                              std::abs(actual.position.x - config.destination_x) <=
                                  config.stopping_distance + .001,
                          "Navigation reported arrival without an actual physical landing");
                    arrived = true;
                    break;
                }
                if (blocked && took_off && actual.position.y < -3)
                    break;
                if (scenario >= 5 && scenario <= 8 && refused)
                    break;
            }
            const bool accepted = scenario == 12                   ? arrived && !took_off
                                  : scenario >= 5 && scenario <= 8 ? refused && !took_off
                                  : scenario == 3 || scenario == 10
                                      ? took_off && airborne && exercised && !arrived
                                      : took_off && (blocked ? max_x < -.5 && !arrived
                                                             : airborne && recovered && arrived);
            if (!accepted)
                throw std::runtime_error("Physical character jump scenario " +
                                         std::to_string(scenario) +
                                         " avoidance=" + std::to_string(avoiding) +
                                         " failed: " + navigation->debug(actor.id()).dump());
        }
}
