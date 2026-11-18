#pragma once
void collision_budget_continuation(const Json& snapshot, const std::filesystem::path& root,
                                   EntityId actor_id) {
    EngineContext engine(WorldRole::Runtime, false, {navigation_module(root), physics_module()});
    Scene scene(engine.world());
    scene.restore_snapshot(snapshot);
    RuntimeSimulation simulation(engine.world(), scene);
    auto navigation =
        std::static_pointer_cast<NavigationRuntime>(engine.world().services().navigation());
    const auto actor = scene.entity(actor_id.str());
    const auto reference = scene.reference(actor_id.str());
    bool crossing = false;
    for (unsigned step = 0; step != 360; ++step) {
        simulation.tick(1.f / 60);
        if (!navigation->debug(actor.id()).at("link").is_null()) {
            crossing = true;
            break;
        }
    }
    check(crossing, "Budget continuation fixture did not enter its first link");
    const auto held = actor.get<LocalTranslation>();
    const auto first = navigation->debug(actor.id()).at("link");
    NavigationLinkCollision volume;
    volume.clearance = .001f;
    actor.set(volume);
    auto fast = actor.get<NavigationAgent>();
    fast.speed = 20;
    actor.set(fast);
    simulation.tick(1.f);
    const auto failed = navigation->agent_state(reference);
    check(failed && failed->phase == NavigationAgentPhase::Failed &&
              failed->status == NavStatus::Invalid && !failed->sampled &&
              actor.get<LocalTranslation>() == held &&
              navigation->debug(actor.id()).at("link") == first &&
              navigation->debug(actor.id())
                      .at("diagnostic")
                      .get<std::string>()
                      .find("query budget exceeded") != std::string::npos,
          "A later link's query failure discarded the original crossing continuation");
    // A larger clearance admits the same path within the bounded query budget.
    volume.clearance = 1;
    actor.set(volume);
    for (unsigned step = 0; step != 4; ++step)
        simulation.tick(1.f);
    check(navigation->agent_state(reference)->phase == NavigationAgentPhase::Arrived,
          "Retained link continuation could not resume after query-budget failure: " +
              navigation->debug(actor.id()).dump());
}

// Existing published-link pipeline supplies the asset, source IDs and route.
void collision_movement(const Json& snapshot, const std::filesystem::path& root, EntityId actor_id,
                        NavigationPlane plane, float arc_height) {
    EngineContext authored;
    Scene source(authored.world());
    source.restore_snapshot(snapshot);
    const auto wall_id = authoring_command(source, "entity.create", {{"name", "Link blocker"}})
                             .at("selected")
                             .get<std::string>();
    auto wall = source.entity(wall_id);
    wall.set<LocalTranslation>({0, 5, 0});
    if (plane == NavigationPlane::PlanarXY)
        wall.set<PhysicsBody2D>({}).set<BoxCollider2D>({.4f, 20});
    else {
        wall.set<PhysicsBody>({}).set<BoxCollider>({.4f, 20, 4});
        const auto floor = authoring_command(source, "entity.create", {{"name", "Physical floor"}})
                               .at("selected")
                               .get<std::string>();
        const auto foot = source.entity(actor_id.str()).get<LocalTranslation>();
        source.entity(floor)
            .set<LocalTranslation>({-3, foot.y - .5, 0})
            .set<PhysicsBody>({})
            .set<BoxCollider>({4, 1, 6});
    }
    source.entity(actor_id.str()).set<NavigationLinkCollision>({});
    const auto configured = source.snapshot();
    {
        EngineContext refused(WorldRole::Runtime, false, {navigation_module(root)});
        Scene candidate(refused.world());
        candidate.restore_snapshot(configured);
        RuntimeSimulation simulation(refused.world(), candidate);
        const auto actor = candidate.entity(actor_id.str());
        actor.set<SpatialBinding>({SpatialMode::World, {}});
        if (plane == NavigationPlane::PlanarXY) {
            CharacterController2D controller;
            controller.mode = 1;
            actor.set<CharacterController2D>(controller);
        } else
            actor.set<CharacterController>({});
        auto agent = actor.get<NavigationAgent>();
        agent.movement = plane == NavigationPlane::PlanarXY ? 2 : 1;
        actor.set<NavigationAgent>(agent);
        const auto before = actor.get<LocalTranslation>();
        simulation.tick(1.f / 60);
        const auto navigation =
            std::static_pointer_cast<NavigationRuntime>(refused.services().navigation());
        const auto feedback = navigation->debug(actor.id());
        const auto expected =
            plane == NavigationPlane::Spatial3D && arc_height > 0
                ? "Character jump requires the physics service"
                : "Character links require grounded 3D or XY movement and positive lift";
        check(feedback.at("phase") == "Failed" && feedback.at("diagnostic") == expected &&
                  actor.get<LocalTranslation>() == before,
              "Character link admission failed to preserve its required boundary: " +
                  feedback.dump());
    }
    EngineContext engine(WorldRole::Runtime, false, {navigation_module(root), physics_module()});
    Scene scene(engine.world());
    scene.restore_snapshot(configured);
    RuntimeSimulation simulation(engine.world(), scene);
    auto physics = std::static_pointer_cast<PhysicsRuntime>(engine.world().services().physics());
    auto navigation =
        std::static_pointer_cast<NavigationRuntime>(engine.world().services().navigation());
    // Initialize the realized query world through its existing zero-time owner
    // boundary; queries themselves never publish pending body changes.
    physics->synchronize(0);
    const auto actor = scene.entity(actor_id.str());
    const auto start = actor.get<LocalTranslation>();
    const auto goal = actor.get<NavigationAgent>();
    const auto reference = scene.reference(actor_id.str());
    const auto run_until = [&](NavigationAgentPhase expected) {
        for (unsigned tick = 0; tick != 360; ++tick) {
            simulation.tick(1.f / 60);
            const auto state = navigation->agent_state(reference);
            check(state && state->status == NavStatus::Success && state->sampled,
                  "Collision traversal lost its admitted navigation sample");
            if (state->phase == expected)
                return;
            check(state->phase != NavigationAgentPhase::Failed,
                  "Collision traversal failed before its expected phase");
        }
        throw std::runtime_error("Navigation collision phase did not settle");
    };
    run_until(NavigationAgentPhase::Blocked);
    const auto held = actor.get<LocalTranslation>();
    check(held.x < 0 && held.x > -1.1,
          "Collision did not stop the agent in front of the gap blocker");
    for (unsigned tick = 0; tick != 4; ++tick)
        simulation.tick(1.f / 60);
    check(actor.get<LocalTranslation>() == held,
          "Blocked collision traversal drifted on repeated queries");
    navigation->tune(reference, "forge.navigation_agent", "speed", 0);
    simulation.tick(1.f / 60);
    check(actor.get<LocalTranslation>() == held &&
              navigation->agent_state(reference)->phase == NavigationAgentPhase::Paused,
          "A collision-held link did not preserve the existing zero-speed hold");
    navigation->tune(reference, "forge.navigation_agent", "speed", goal.speed);
    for (const auto* field : {"enabled", "has_destination"}) {
        navigation->tune(reference, "forge.navigation_agent", field, false);
        simulation.tick(1.f / 60);
        check(actor.get<LocalTranslation>() == held,
              "Disabling a collision-held agent changed its retained pose");
        navigation->tune(reference, "forge.navigation_agent", field, true);
    }
    navigation->tune(reference, "forge.navigation_agent", "destination_x", 3.5);
    run_until(NavigationAgentPhase::Blocked);
    check(actor.get<LocalTranslation>() == held,
          "Retargeting a collision-held link discarded its crossing");
    const auto before_tuning = actor.get<NavigationLinkCollision>();
    const auto before_rejection = scene.snapshot();
    for (const auto& bad :
         {Json{{"field", "mask"}, {"value", .5}}, Json{{"field", "mask"}, {"value", -1}},
          Json{{"field", "mask"}, {"value", std::uint64_t{1} << 32}},
          Json{{"field", "radius"}, {"value", 0}}, Json{{"field", "straight_height"}, {"value", 0}},
          Json{{"field", "clearance"}, {"value", 2}}}) {
        bool refused = false;
        try {
            navigation->tune(reference, "forge.navigation_link_collision",
                             bad.at("field").get<std::string>(), bad.at("value"));
        } catch (const std::exception&) {
            refused = true;
        }
        check(refused && actor.get<NavigationLinkCollision>() == before_tuning &&
                  scene.snapshot() == before_rejection,
              "Invalid live collision tuning changed native or Scene values");
    }
    const auto checkpoint = navigation->checkpoint();
    check(checkpoint.at("version").get<unsigned>() == 6u &&
              checkpoint.at("agents").at(0).contains("collision"),
          "Collision traversal did not use its semantic recovery profile");
    const auto recover_blocked = [&](const Json& image) {
        EngineContext recovered(WorldRole::Runtime, false,
                                {navigation_module(root), physics_module()});
        Scene restored(recovered.world());
        restored.restore_snapshot(scene.snapshot());
        RuntimeSimulation resumed(recovered.world(), restored);
        auto restored_physics =
            std::static_pointer_cast<PhysicsRuntime>(recovered.world().services().physics());
        restored_physics->synchronize(0);
        auto service =
            std::static_pointer_cast<NavigationRuntime>(recovered.world().services().navigation());
        service->restore(image);
        resumed.tick(1.f / 60);
        check(restored.entity(actor_id.str()).get<LocalTranslation>() == held &&
                  service->agent_state(restored.reference(actor_id.str()))->phase ==
                      NavigationAgentPhase::Blocked,
              "Recovered blocked crossing changed its on-curve pose");
    };
    recover_blocked(checkpoint);
    const auto extra_id =
        authoring_command(scene, "entity.create", {{"name", "Inactive character"}})
            .at("selected")
            .get<std::string>();
    NavigationAgent inactive;
    inactive.enabled = false;
    inactive.movement = plane == NavigationPlane::PlanarXY ? 2 : 1;
    scene.entity(extra_id).set<NavigationAgent>(inactive);
    const auto mixed = navigation->checkpoint();
    check(mixed.at("version").get<unsigned>() == (plane == NavigationPlane::PlanarXY ? 8u : 7u) &&
              mixed.at("agents").size() == 2,
          "Character selection lost the mixed crossing recovery profile");
    recover_blocked(mixed);
    for (const bool downgrade : {false, true}) {
        auto bad = mixed;
        if (downgrade)
            bad["version"] = 6;
        else {
            for (auto& entry : bad.at("agents"))
                if (entry.at("config").value("movement", 0u) != 0)
                    entry.erase("collision");
        }
        EngineContext refused(WorldRole::Runtime, false,
                              {navigation_module(root), physics_module()});
        Scene candidate(refused.world());
        candidate.restore_snapshot(scene.snapshot());
        RuntimeSimulation unused(refused.world(), candidate);
        auto service = std::static_pointer_cast<NavigationRuntime>(refused.services().navigation());
        bool rejected = false;
        try {
            service->restore(bad);
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected, "Incomplete or downgraded mixed character/collision recovery was admitted");
    }
    scene.delete_subtree(extra_id);
    auto runtime_wall = scene.entity(wall_id);
    if (plane == NavigationPlane::PlanarXY) {
        auto body = runtime_wall.get<PhysicsBody2D>();
        body.enabled = false;
        runtime_wall.set(body);
    } else {
        auto body = runtime_wall.get<PhysicsBody>();
        body.enabled = false;
        runtime_wall.set(body);
    }
    run_until(NavigationAgentPhase::Arrived);
    check(actor.get<LocalTranslation>().x > 3.4,
          "A collision-held link did not replan to its changed goal after exiting");

    const auto reset = [&](NavigationLinkCollision volume, bool sensor) {
        actor.set(start).set(goal).set(volume);
        if (plane == NavigationPlane::PlanarXY) {
            auto body = runtime_wall.get<PhysicsBody2D>();
            body.enabled = true;
            body.sensor = sensor;
            runtime_wall.set(body);
        } else {
            auto body = runtime_wall.get<PhysicsBody>();
            body.enabled = true;
            body.sensor = sensor;
            runtime_wall.set(body);
        }
        physics->synchronize(0);
    };
    NavigationLinkCollision volume;
    reset(volume, false);
    check(navigation->tune(reference, "forge.navigation_link_collision", "mask", 2u)
                  .get<std::uint32_t>() == 2u,
          "Collision mask did not tune through the existing navigation owner");
    check(actor.get<NavigationLinkCollision>().mask == 2,
          "Live mask tuning did not change the query filter");
    run_until(NavigationAgentPhase::Arrived);
    volume.mask = UINT32_MAX;
    reset(volume, true);
    run_until(NavigationAgentPhase::Arrived);
    volume.include_sensors = true;
    reset(volume, true);
    run_until(NavigationAgentPhase::Blocked);
    volume.enabled = false;
    reset(volume, false);
    run_until(NavigationAgentPhase::Arrived);
    volume.enabled = true;
    volume.mask = 0;
    reset(volume, false);
    run_until(NavigationAgentPhase::Arrived);
    volume.mask = UINT32_MAX;
    reset(volume, false);
    const auto entry = checkpoint.at("agents").at(0).at("link").at("entry").get<Double3>();
    runtime_wall.set<LocalTranslation>({entry[0], 5, 0});
    physics->synchronize(0);
    run_until(NavigationAgentPhase::Blocked);
    const auto overlap = actor.get<LocalTranslation>();
    check(std::hypot(overlap.x - entry[0], overlap.y - entry[1], overlap.z - entry[2]) < 1e-6,
          "Initial overlap did not hold the navigation link entry");
    if (checkpoint.at("agents").at(0).at("link").contains("arc_height")) {
        volume.mask = 2;
        volume.clearance = .001f;
        reset(volume, false);
        auto fast = goal;
        fast.speed = 20;
        actor.set(fast);
        simulation.tick(1.f);
        const auto failed = navigation->agent_state(reference);
        check(failed && failed->phase == NavigationAgentPhase::Failed &&
                  failed->status == NavStatus::Invalid && !failed->sampled &&
                  actor.get<LocalTranslation>() == start &&
                  navigation->debug(actor.id()).at("link").is_null() &&
                  navigation->debug(actor.id())
                          .at("diagnostic")
                          .get<std::string>()
                          .find("query budget exceeded") != std::string::npos,
              "Collision query budget partially published pose or crossing state: " +
                  navigation->debug(actor.id()).dump());
    }
    if (plane == NavigationPlane::PlanarXY)
        check(actor.get<LocalTranslation>().z == start.z,
              "Collision queries changed XY visual depth");
    {
        EngineContext without_physics(WorldRole::Runtime, false, {navigation_module(root)});
        Scene unbound(without_physics.world());
        unbound.restore_snapshot(configured);
        RuntimeSimulation unchecked(without_physics.world(), unbound);
        auto service =
            std::static_pointer_cast<NavigationRuntime>(without_physics.services().navigation());
        const auto missing_actor = unbound.entity(actor_id.str());
        const auto missing_ref = unbound.reference(actor_id.str());
        bool refused = false;
        for (unsigned step = 0; step != 360; ++step) {
            unchecked.tick(1.f / 60);
            const auto state = service->agent_state(missing_ref);
            if (state && state->phase == NavigationAgentPhase::Failed) {
                check(state->status == NavStatus::Unavailable && !state->sampled,
                      "Missing physics had the wrong navigation failure scope");
                refused = true;
                break;
            }
        }
        check(refused && missing_actor.get<LocalTranslation>().x < 0,
              "Missing physics silently bypassed crossing collision checks");
        auto disabled = missing_actor.get<NavigationLinkCollision>();
        disabled.enabled = false;
        missing_actor.set(disabled);
        bool arrived = false;
        for (unsigned step = 0; step != 360; ++step) {
            unchecked.tick(1.f / 60);
            if (service->agent_state(missing_ref)->phase == NavigationAgentPhase::Arrived) {
                arrived = true;
                break;
            }
        }
        check(arrived, "Disabled collision checks still required a physics service");
    }
}
