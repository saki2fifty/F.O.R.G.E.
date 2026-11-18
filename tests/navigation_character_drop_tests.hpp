#pragma once
#include <cmath>

void character_drops(const Json& source, const std::filesystem::path& root, EntityId actor_id,
                     bool planar) {
    for (unsigned scenario = 0; scenario < 16; ++scenario) {
        const bool blocked = scenario == 1;
        EngineContext engine(WorldRole::Runtime, false,
                             {navigation_module(root), physics_module({}, root)});
        Scene scene(engine.world());
        scene.restore_snapshot(source);
        RuntimeSimulation simulation(engine.world(), scene);
        const auto physics = std::static_pointer_cast<PhysicsRuntime>(engine.services().physics());
        const auto navigation =
            std::static_pointer_cast<NavigationRuntime>(engine.services().navigation());
        const auto actor = scene.entity(actor_id.str());
        const auto ref = scene.reference(actor_id.str());
        const auto visual_z = actor.get<LocalTranslation>().z;
        actor.set<SpatialBinding>({SpatialMode::World, {}});
        if (planar) {
            CharacterController2D controller;
            controller.gravity_factor = 1;
            actor.set<CharacterController2D>(controller);
        } else {
            actor.set<CharacterController>({});
        }
        auto agent = actor.get<NavigationAgent>();
        agent.movement = planar ? 3 : 1;
        agent.speed = 6;
        agent.enabled = false;
        if (scenario == 9 || scenario == 10 || scenario == 14) {
            auto position = actor.get<LocalTranslation>();
            position.x = -2;
            actor.set<LocalTranslation>(position);
        }
        if (scenario == 8)
            actor.set<NavigationAvoidance>({});
        if (scenario == 10) {
            agent.stopping_distance = 5;
            agent.destination_x = 2;
        }
        actor.set<NavigationAgent>(agent);
        const auto solid = [&](const char* name, double x, double y, float width, float height) {
            const auto id = authoring_command(scene, "entity.create", {{"name", name}})
                                .at("selected")
                                .get<std::string>();
            auto entity = scene.entity(id);
            entity.set<SpatialBinding>({SpatialMode::World, {}}).set<LocalTranslation>({x, y, 0});
            if (planar)
                entity.set<PhysicsBody2D>({}).set<BoxCollider2D>({width, height});
            else
                entity.set<PhysicsBody>({}).set<BoxCollider>({width, height, 8});
            return entity;
        };
        const auto launch = solid("Drop launch", -3, planar ? -.5 : -.1, 4, planar ? 1 : .2f);
        const auto landing = solid("Drop landing", 3, planar ? -1.5 : -1.1, 4, planar ? 1 : .2f);
        flecs::entity wall;
        if (blocked || scenario == 12)
            wall = solid("Drop wall", scenario == 12 ? -1.2 : 0, 3, .3f, 20);
        if (scenario == 15) {
            PhysicsConfig config;
            config.gravity = {0, 9.81, 0};
            physics->configure(config);
        }
        for (unsigned i = 0; i < (scenario == 9 ? 0u : 10u); ++i)
            simulation.tick(1.f / 60);
        if (scenario == 2 || scenario == 3) {
            const auto body = scenario == 2 ? launch : landing;
            if (planar) {
                auto config = body.get<PhysicsBody2D>();
                if (scenario == 2)
                    config.motion = 1;
                else
                    config.enabled = false;
                body.set<PhysicsBody2D>(config);
            } else {
                auto config = body.get<PhysicsBody>();
                if (scenario == 2)
                    config.motion = 1;
                else
                    config.enabled = false;
                body.set<PhysicsBody>(config);
            }
            physics->synchronize(0);
        }
        if (scenario == 13) {
            landing.set<LocalTranslation>({2, planar ? -2 : -1.2, 0})
                .set<LocalRotation>({0, 0, .5f, float(std::sqrt(3.) * .5)});
            physics->synchronize(0);
        }
        if (scenario == 9) {
            check(planar ? !physics->find_character_2d(ref) : !physics->find_character(ref),
                  "Cold controller query fabricated a native observation");
            bool unavailable = false;
            try {
                if (planar)
                    (void)physics->character_2d(ref);
                else
                    (void)physics->character(ref);
            } catch (const std::exception&) {
                unavailable = true;
            }
            check(unavailable, "Required cold character getter lost its unavailable error");
        }
        const auto actual = [&](PhysicsService& service) {
            if (planar) {
                const auto value = service.character_2d(ref);
                return Double3{value.position[0], value.position[1], visual_z};
            }
            const auto value = service.character(ref);
            return Double3{value.position.x, value.position.y, value.position.z};
        };
        agent.enabled = true;
        actor.set<NavigationAgent>(agent);
        bool fell = false, recovered = false, arrived = false, failed = false;
        bool exercised = false, released = false;
        unsigned pause_ticks = 0, release_ticks = 0;
        Double3 held{};
        for (unsigned i = 0; i < 360; ++i) {
            simulation.tick(scenario == 14 ? .2f : 1.f / 60);
            const auto position = actual(*physics);
            const auto debug = navigation->debug(actor.id());
            const auto phase = debug.at("phase").get<std::string>();
            const bool airborne = planar
                                      ? physics->character_2d(ref).ground == CharacterGround::InAir
                                      : physics->character(ref).ground == CharacterGround::InAir;
            const bool impulse = planar ? physics->character_2d(ref).jump_accepted
                                        : physics->character(ref).jump_accepted;
            check(!impulse, "A drop manufactured an upward jump impulse");
            if (planar)
                check(actor.get<LocalTranslation>().z == visual_z,
                      "Drop changed independent visual depth");
            fell |= airborne && position[1] < -.05;
            if (pause_ticks == 1) {
                check(std::abs(position[0] - held[0]) < 1e-6 && position[1] < held[1] &&
                          phase == "Paused",
                      "Drop speed pause froze gravity or retained steering");
                agent.speed = 6;
                actor.set<NavigationAgent>(agent);
                pause_ticks = 2;
            }
            if (scenario == 11 && exercised) {
                check(std::abs(position[0] - held[0] + 2 * double(float(1.f / 60))) < 1e-5 &&
                          position[1] < held[1],
                      "Drop steering overrode manual movement");
                released = true;
                break;
            }
            if ((scenario == 5 || scenario == 6) && exercised) {
                check(std::abs(position[0] - held[0]) < 1e-6 && position[1] < held[1],
                      "Cancelled drop retained steering or suspended gravity");
                if (++release_ticks == 10) {
                    released = true;
                    break;
                }
            }
            if (fell && airborne && !exercised && scenario >= 4 && scenario <= 7) {
                held = position;
                if (scenario == 4) {
                    agent.speed = 0;
                    pause_ticks = 1;
                }
                if (scenario == 5)
                    agent.enabled = false;
                if (scenario == 6)
                    agent.has_destination = false;
                if (scenario == 7)
                    agent.destination_x = 4;
                actor.set<NavigationAgent>(agent);
                exercised = true;
            }
            if (scenario == 11 && airborne && position[1] < -.2 && !exercised) {
                held = position;
                if (planar)
                    physics->move_character_2d(ref, {-2, physics->character_2d(ref).velocity[1]});
                else
                    physics->move_character(ref, {-2, 0, 0});
                exercised = true;
            }
            if (scenario == 12 && phase == "Blocked" && !exercised) {
                check(!fell, "Drop wall-before-walkoff fabricated a fall");
                if (planar) {
                    auto config = wall.get<PhysicsBody2D>();
                    config.enabled = false;
                    wall.set<PhysicsBody2D>(config);
                } else {
                    auto config = wall.get<PhysicsBody>();
                    config.enabled = false;
                    wall.set<PhysicsBody>(config);
                }
                exercised = true;
            }
            if (scenario == 0 && fell && airborne && !recovered) {
                const auto saved_scene = scene.snapshot();
                const auto saved_physics = physics->checkpoint();
                const auto saved_navigation = navigation->checkpoint();
                check(saved_navigation.at("version").get<std::uint64_t>() == 12 &&
                          saved_navigation.at("agents").at(0).at("translation").is_null() &&
                          saved_navigation.at("agents")
                              .at(0)
                              .at("link")
                              .at("physical_drop")
                              .get<bool>(),
                      "Drop recovery lost intent or duplicated native pose ownership");
                EngineContext restored(WorldRole::Runtime, false,
                                       {navigation_module(root), physics_module({}, root)});
                Scene restored_scene(restored.world());
                restored_scene.restore_snapshot(saved_scene);
                RuntimeSimulation resumed(restored.world(), restored_scene);
                const auto restored_physics =
                    std::static_pointer_cast<PhysicsRuntime>(restored.services().physics());
                const auto restored_navigation =
                    std::static_pointer_cast<NavigationRuntime>(restored.services().navigation());
                restored_physics->restore(saved_physics);
                restored_navigation->restore(saved_navigation);
                for (unsigned step = 0; step < 3; ++step) {
                    simulation.tick(1.f / 60);
                    resumed.tick(1.f / 60);
                    const auto a = actual(*physics), b = actual(*restored_physics);
                    check(std::hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]) < 1e-6,
                          "Recovered native drop diverged");
                }
                for (unsigned bad = 0; bad < 10; ++bad) {
                    auto damaged = saved_navigation;
                    auto& row = damaged["agents"].at(0);
                    if (bad == 0)
                        damaged["version"] = 11;
                    if (bad == 1)
                        damaged["version"] = 10;
                    if (bad == 2)
                        row["link"]["physical_drop"] = false;
                    if (bad == 3)
                        row["link"]["physical_drop"] = "true";
                    if (bad == 4)
                        row["link"].erase("physical_drop");
                    if (bad == 5)
                        row["link"]["physical_jump"] = true;
                    if (bad == 6)
                        row["link"]["exit"][1] = row["link"]["entry"][1];
                    if (bad == 7)
                        row["translation"] = Json::array({0, 0, 0});
                    if (bad == 8)
                        damaged["agents"] = Json::array();
                    if (bad == 9) {
                        row["link"]["exit"][0] = row["link"]["entry"][0];
                        row["link"]["exit"][2] = row["link"]["entry"][2];
                    }
                    EngineContext candidate(WorldRole::Runtime, false,
                                            {navigation_module(root), physics_module({}, root)});
                    Scene candidate_scene(candidate.world());
                    candidate_scene.restore_snapshot(saved_scene);
                    if (bad == 8)
                        candidate_scene.entity(actor_id.str()).remove<NavigationAgent>();
                    std::static_pointer_cast<PhysicsRuntime>(candidate.services().physics())
                        ->restore(saved_physics);
                    bool rejected = false;
                    try {
                        std::static_pointer_cast<NavigationRuntime>(
                            candidate.services().navigation())
                            ->restore(damaged);
                    } catch (const std::exception&) {
                        rejected = true;
                    }
                    check(rejected, "Malformed drop recovery was accepted");
                }
                recovered = true;
            }
            if (phase == "Arrived") {
                check(scenario == 10 ? !airborne && !fell && std::abs(position[0] + 2) < 1e-6
                                     : !airborne && std::abs(position[1] + 1) < .05 &&
                                           std::abs(position[0] - agent.destination_x) <=
                                               agent.stopping_distance + .001,
                      "Drop arrived without an actual supported lower landing");
                arrived = true;
                break;
            }
            if (phase == "Failed") {
                failed = true;
                break;
            }
        }
        const bool refused = scenario == 2 || scenario == 3 || scenario >= 13;
        const bool cancelled = scenario == 5 || scenario == 6 || scenario == 11;
        const bool accepted = refused          ? failed && !fell && !arrived
                              : scenario == 10 ? arrived && !fell
                              : cancelled      ? fell && exercised && released && !arrived
                              : blocked ? fell && failed && !arrived && actual(*physics)[0] < -.4
                                        : fell && arrived && (scenario != 0 || recovered) &&
                                              (scenario != 4 || pause_ticks == 2) &&
                                              ((scenario != 7 && scenario != 12) || exercised);
        check(accepted, "Physical drop scenario=" + std::to_string(scenario) +
                            " failed: " + navigation->debug(actor.id()).dump());
    }
}
