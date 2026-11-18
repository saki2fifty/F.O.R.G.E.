#pragma once

void mixed_character_link_recovery(const Json& source, const std::filesystem::path& root,
                                   const std::filesystem::path& worker, EntityId planar_id,
                                   bool include_drop = false) {
    EngineContext author;
    Scene authored(author.world());
    authored.restore_snapshot(source);
    const auto create = [&](const char* name, LocalTranslation position) {
        const auto id = authoring_command(authored, "entity.create", {{"name", name}})
                            .at("selected")
                            .get<std::string>();
        auto entity = authored.entity(id);
        entity.set<SpatialBinding>({SpatialMode::World, {}}).set<LocalTranslation>(position);
        return entity;
    };
    const auto left = create("Mixed spatial launch", {-3, 0, 10});
    const auto right = create("Mixed spatial landing", {3, 2, 10});
    for (const auto entity : {left, right})
        entity.set<Primitive>({3}).set<LocalScale>({4, 1, 6}).set<NavigationSurface>({});
    NavigationLink link;
    link.start_x = .25f;
    link.end_x = 1.25f;
    link.end_y = 2;
    link.arc_height = 1;
    left.set<NavigationLink>(link);
    NavigationSettings settings;
    settings.profile = "MixedSpatial";
    // The authored link uses the same selected profile as the spatial surfaces.
    link.profile = settings.profile;
    left.set<NavigationLink>(link);
    const auto selected = authored.effective_document();
    const auto asset = prepare_navigation(root, selected, settings, worker).publish(selected).id;
    for (const auto position : {LocalTranslation{-3, -.1, 10}, LocalTranslation{3, 1.9, 10}})
        create("Mixed spatial floor", position).set<PhysicsBody>({}).set<BoxCollider>({4, .2f, 6});
    for (double x : {-3., 3.})
        create("Mixed planar floor", {x, include_drop && x > 0 ? -1.5 : -.5, 0})
            .set<PhysicsBody2D>({})
            .set<BoxCollider2D>({4, 1});
    const auto planar = authored.entity(planar_id.str());
    CharacterController2D controller;
    controller.gravity_factor = 1;
    auto agent = planar.get<NavigationAgent>();
    agent.movement = 3;
    agent.speed = 6;
    planar.set<SpatialBinding>({SpatialMode::World, {}})
        .set<CharacterController2D>(controller)
        .set<NavigationAgent>(agent);
    agent.navmesh = {asset};
    agent.movement = 1;
    agent.destination_y = 2;
    agent.destination_z = 10;
    const auto spatial = create("Mixed spatial character", {-3, .1, 10});
    spatial.set<CharacterController>({}).set<NavigationAgent>(agent);
    const auto spatial_id = spatial.get<PersistentEntityId>().value;
    EntityId extra_id;
    if (include_drop) {
        NavigationLink xy_link;
        xy_link.plane = 1;
        xy_link.profile = "MixedPlanarJump";
        xy_link.start_x = 1;
        xy_link.end_x = 5;
        xy_link.end_y = -1;
        xy_link.arc_height = 1;
        create("Mixed XY positive link", {-3, 0, 0}).set<NavigationLink>(xy_link);
        NavigationSettings xy_settings;
        xy_settings.plane = NavigationPlane::PlanarXY;
        xy_settings.profile = xy_link.profile;
        const auto xy_source = authored.effective_document();
        const auto xy_asset =
            prepare_navigation(root, xy_source, xy_settings, worker).publish(xy_source).id;
        auto extra_agent = planar.get<NavigationAgent>();
        extra_agent.navmesh = {xy_asset};
        const auto extra = create("Mixed XY jump character", {-4, 0, 37.125});
        extra.set<CharacterController2D>(controller).set<NavigationAgent>(extra_agent);
        extra_id = extra.get<PersistentEntityId>().value;
    }
    EngineContext engine(WorldRole::Runtime, false,
                         {navigation_module(root), physics_module({}, root)});
    Scene scene(engine.world());
    scene.restore_snapshot(authored.snapshot());
    RuntimeSimulation simulation(engine.world(), scene);
    const auto physics = std::static_pointer_cast<PhysicsRuntime>(engine.services().physics());
    const auto navigation =
        std::static_pointer_cast<NavigationRuntime>(engine.services().navigation());
    const auto xy = scene.reference(planar_id.str()), xyz = scene.reference(spatial_id.str());
    bool all_crossing = false;
    for (unsigned tick = 0; tick < 180; ++tick) {
        simulation.tick(1.f / 60);
        const auto saved = navigation->checkpoint();
        if (saved.at("version").get<std::uint64_t>() != (include_drop ? 12u : 11u))
            continue;
        unsigned jumps = 0, drops = 0;
        for (const auto& row : saved.at("agents")) {
            if (row.contains("link")) {
                jumps += row.at("link").value("physical_jump", false);
                drops += row.at("link").value("physical_drop", false);
                check(row.at("translation").is_null(),
                      "Mixed crossings duplicated native pose ownership");
            }
        }
        if (jumps != 2 || drops != unsigned(include_drop) ||
            (include_drop && physics->character_2d(xy).ground != CharacterGround::InAir))
            continue;
        all_crossing = true;
        EngineContext restored(WorldRole::Runtime, false,
                               {navigation_module(root), physics_module({}, root)});
        Scene restored_scene(restored.world());
        restored_scene.restore_snapshot(scene.snapshot());
        RuntimeSimulation resumed(restored.world(), restored_scene);
        const auto restored_physics =
            std::static_pointer_cast<PhysicsRuntime>(restored.services().physics());
        const auto restored_navigation =
            std::static_pointer_cast<NavigationRuntime>(restored.services().navigation());
        restored_physics->restore(physics->checkpoint());
        restored_navigation->restore(saved);
        for (unsigned step = 0; step < 5; ++step) {
            simulation.tick(1.f / 60);
            resumed.tick(1.f / 60);
            const auto a = physics->character_2d(xy), b = restored_physics->character_2d(xy);
            const auto c = physics->character(xyz), d = restored_physics->character(xyz);
            check(a.position == b.position &&
                      std::hypot(c.position.x - d.position.x, c.position.y - d.position.y,
                                 c.position.z - d.position.z) < 1e-6,
                  "Mixed physical3D/XY navigation recovery diverged");
            if (include_drop) {
                const auto extra_ref = scene.reference(extra_id.str());
                check(physics->character_2d(extra_ref).position ==
                          restored_physics->character_2d(extra_ref).position,
                      "Mixed drop recovery lost its concurrent XY positive jump");
            }
        }
        break;
    }
    check(all_crossing, "Mixed recovery never observed concurrent physical crossings");
}

void planar_character_jumps(const Json& source, const std::filesystem::path& root,
                            EntityId actor_id, float lift, bool reverse = false,
                            double landing_height = 0) {
    for (unsigned scenario = 0; scenario < 10; ++scenario) {
        const bool blocked = scenario == 1;
        const double direction = reverse ? -1 : 1;
        const double start_height = reverse ? landing_height : 0;
        const double rise = reverse ? -landing_height : landing_height;
        const double apex = start_height + (rise + 4 * lift) * (rise + 4 * lift) / (16 * lift);
        EngineContext engine(WorldRole::Runtime, false,
                             {navigation_module(root), physics_module({}, root)});
        Scene scene(engine.world());
        scene.restore_snapshot(source);
        RuntimeSimulation simulation(engine.world(), scene);
        const auto physics = std::static_pointer_cast<PhysicsRuntime>(engine.services().physics());
        const auto navigation =
            std::static_pointer_cast<NavigationRuntime>(engine.services().navigation());
        const auto actor = scene.entity(actor_id.str());
        const auto reference = engine.world().reference(actor.id()).value();
        const auto visual_z = actor.get<LocalTranslation>().z;
        CharacterController2D controller;
        controller.gravity_factor = 1;
        actor.set<SpatialBinding>({SpatialMode::World, {}}).set<CharacterController2D>(controller);
        auto agent = actor.get<NavigationAgent>();
        agent.movement = 3;
        agent.speed = 6;
        agent.enabled = false;
        if (reverse || scenario == 8) {
            auto position = actor.get<LocalTranslation>();
            position.x = scenario == 8 ? -direction * 2 : 3;
            position.y = start_height;
            actor.set<LocalTranslation>(position);
        }
        if (reverse) {
            agent.destination_x = -3;
            agent.destination_y = 0;
        }
        if (scenario == 9)
            actor.set<NavigationAvoidance>({});
        actor.set<NavigationAgent>(agent);
        const auto solid = [&](const char* name, double x, double y, float width, float height) {
            const auto id = authoring_command(scene, "entity.create", {{"name", name}})
                                .at("selected")
                                .get<std::string>();
            scene.entity(id)
                .set<SpatialBinding>({SpatialMode::World, {}})
                .set<LocalTranslation>({x, y, 0})
                .set<PhysicsBody2D>({})
                .set<BoxCollider2D>({width, height});
            return scene.entity(id);
        };
        const auto launch = solid("XY launch floor", -3, -.5, 4, 1);
        const auto landing = solid("XY landing floor", 3, landing_height - .5, 4, 1);
        if (blocked)
            solid("XY flight obstruction", 0, 3, .3f, 20);
        for (unsigned i = 0; i < (scenario == 8 ? 0u : 10u); ++i)
            simulation.tick(1.f / 60);
        if (scenario == 2 || scenario == 3) {
            const auto body =
                scenario == 2 ? (reverse ? landing : launch) : (reverse ? launch : landing);
            auto settings = body.get<PhysicsBody2D>();
            if (scenario == 2)
                settings.motion = 1;
            else
                settings.enabled = false;
            body.set<PhysicsBody2D>(settings);
            physics->synchronize(0);
        }
        if (scenario == 4)
            agent.speed = 2;
        agent.enabled = true;
        actor.set<NavigationAgent>(agent);
        bool took_off = false, recovered = false, arrived = false, failed = false;
        bool exercised = false, cancelled_landed = false;
        unsigned pause_ticks = 0;
        Double2 held{};
        double highest = 0, furthest = -3;
        for (unsigned i = 0; i < 480; ++i) {
            simulation.tick(1.f / 60);
            const auto actual = physics->character_2d(reference);
            const auto debug = navigation->debug(actor.id());
            const auto phase = debug.at("phase").get<std::string>();
            took_off |= actual.jump_accepted;
            highest = std::max(highest, actual.position[1]);
            furthest = std::max(furthest, actual.position[0] * (reverse ? -1 : 1));
            check(actor.get<LocalTranslation>().z == visual_z,
                  "Physical XY jump changed independent visual depth");
            if (scenario == 6 && pause_ticks == 1) {
                check(std::abs(actual.position[0] - held[0]) < 1e-6 &&
                          actual.position[1] != held[1] && phase == "Paused",
                      "Paused XY steering froze gravity or retained horizontal flight intent");
                agent.speed = 6;
                actor.set<NavigationAgent>(agent);
                pause_ticks = 2;
            }
            if (took_off && !exercised && scenario >= 5 && scenario <= 7) {
                held = actual.position;
                if (scenario == 5)
                    agent.enabled = false;
                if (scenario == 6) {
                    agent.speed = 0;
                    pause_ticks = 1;
                }
                if (scenario == 7)
                    agent.destination_x = direction * 4;
                actor.set<NavigationAgent>(agent);
                exercised = true;
            }
            if (scenario == 5 && exercised && actual.ground == CharacterGround::OnGround &&
                !actual.jump_accepted && actual.velocity[1] <= 0) {
                check(std::abs(actual.position[0] - held[0]) < 1e-5,
                      "Disabling XY navigation retained flight steering");
                cancelled_landed = true;
                break;
            }
            if (scenario == 0 && actual.ground == CharacterGround::InAir && took_off &&
                !recovered) {
                const auto saved_navigation = navigation->checkpoint();
                const auto saved_scene = scene.snapshot();
                const auto saved_physics = physics->checkpoint();
                check(saved_navigation.at("version").get<std::uint64_t>() == 11 &&
                          saved_navigation.at("agents").at(0).at("translation").is_null(),
                      "XY physical jump recovery duplicated pose authority or omitted its profile");
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
                    const auto a = physics->character_2d(reference),
                               b = restored_physics->character_2d(reference);
                    check(std::hypot(a.position[0] - b.position[0], a.position[1] - b.position[1]) <
                              1e-6,
                          "Recovered XY physical jump diverged from native flight");
                }
                recovered = true;
                if (!reverse && landing_height == 0)
                    for (unsigned bad = 0; bad < 10; ++bad) {
                        auto damaged = saved_navigation;
                        auto& row = damaged["agents"].at(0);
                        if (bad == 0)
                            damaged["version"] = 10;
                        if (bad == 1)
                            damaged["version"] = 9;
                        if (bad == 2)
                            row["link"]["physical_jump"] = false;
                        if (bad == 3)
                            row["link"]["physical_jump"] = "true";
                        if (bad == 4)
                            row["link"].erase("physical_jump");
                        if (bad == 5)
                            row["link"]["source"] = EntityId::generate();
                        if (bad == 6)
                            row["link"]["exit"][0] = 20;
                        if (bad == 7)
                            row["translation"] = Json::array({0, 0, 0});
                        if (bad == 8)
                            row["link"]["arc_height"] = 0;
                        if (bad == 9)
                            damaged["agents"] = Json::array();
                        EngineContext candidate(
                            WorldRole::Runtime, false,
                            {navigation_module(root), physics_module({}, root)});
                        Scene candidate_scene(candidate.world());
                        candidate_scene.restore_snapshot(saved_scene);
                        const auto candidate_physics = std::static_pointer_cast<PhysicsRuntime>(
                            candidate.services().physics());
                        const auto candidate_navigation =
                            std::static_pointer_cast<NavigationRuntime>(
                                candidate.services().navigation());
                        candidate_physics->restore(saved_physics);
                        bool rejected = false;
                        try {
                            candidate_navigation->restore(damaged);
                        } catch (const std::exception&) {
                            rejected = true;
                        }
                        check(rejected, "Malformed or empty XY jump recovery was admitted");
                    }
            }
            if (phase == "Arrived") {
                check(actual.ground == CharacterGround::OnGround &&
                          std::abs(actual.position[0] - agent.destination_x) <=
                              agent.stopping_distance + .001 &&
                          std::abs(actual.position[1] - agent.destination_y) < .02,
                      "XY navigation reported arrival without an actual supported landing");
                arrived = true;
                break;
            }
            if (phase == "Failed") {
                failed = true;
                break;
            }
        }
        const auto final = navigation->debug(actor.id());
        const std::string_view expected_error = scenario == 2   ? "static walkable support"
                                                : scenario == 3 ? "static walkable support"
                                                : scenario == 4 ? "speed, lift or fixed-step limits"
                                                                : "";
        const bool admission_refused = !expected_error.empty();
        const bool accepted =
            admission_refused
                ? !took_off && failed && !arrived &&
                      final.at("diagnostic").get<std::string>().find(expected_error) !=
                          std::string::npos
            : scenario == 5 ? took_off && exercised && cancelled_landed && !arrived
            : blocked ? took_off && failed && !arrived && furthest < -.4
                      : took_off && arrived && std::abs(highest - apex) < .05 &&
                            (scenario != 0 || recovered) && (scenario != 6 || pause_ticks == 2) &&
                            (scenario != 7 || exercised);
        if (!accepted)
            throw std::runtime_error(
                "XY physical jump scenario=" + std::to_string(scenario) +
                " blocked=" + std::to_string(blocked) + " reverse=" + std::to_string(reverse) +
                " takeoff=" + std::to_string(took_off) + " recovered=" + std::to_string(recovered) +
                " highest=" + std::to_string(highest) + " lift=" + std::to_string(lift) +
                " apex=" + std::to_string(apex) + " furthest=" + std::to_string(furthest) +
                " failed: " + navigation->debug(actor.id()).dump());
    }
}
