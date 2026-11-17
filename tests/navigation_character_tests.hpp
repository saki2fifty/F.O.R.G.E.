#pragma once

inline void navigation_character_tests(const std::filesystem::path& root, const Json& source) {
    Fixture run(root, true);
    run.scene.restore_snapshot(source);
    auto actor = run.scene.entity("agent");
    check(actor.get<NavigationAgent>().movement == 0,
          "Legacy navigation did not retain its default movement owner");
    const auto create = [&](const char* name, LocalTranslation position, BoxCollider shape) {
        const auto id = authoring_command(run.scene, "entity.create", {{"name", name}})
                            .at("selected")
                            .get<std::string>();
        auto entity = run.scene.entity(id);
        entity.set<LocalTranslation>(position);
        entity.set<SpatialBinding>({SpatialMode::World, {}});
        entity.set<PhysicsBody>({});
        entity.set<BoxCollider>(shape);
        return entity;
    };
    create("Character ground", {0, -.5, 0}, {20, 1, 20});
    auto wall = create("Unbaked character wall", {-7, 1.5, 0}, {.2, 3, 20});
    actor.set<SpatialBinding>({SpatialMode::World, {}});
    actor.set<CharacterController>({});
    auto config = actor.get<NavigationAgent>();
    config.movement = 1;
    config.destination_x = -4;
    config.destination_y = .1;
    config.destination_z = 0;
    actor.set<NavigationAgent>(config);
    const auto reference = run.engine.world().reference(actor.id()).value();
    const auto configured = run.scene.snapshot();
    for (bool avoiding : {false, true}) {
        Fixture edge(root, true);
        edge.scene.restore_snapshot(configured);
        const auto character = edge.scene.entity("agent");
        if (avoiding)
            character.set<NavigationAvoidance>({});
        auto agent = character.get<NavigationAgent>();
        NavResult projection;
        for (double x = 9.9; x > 8; x -= .05) {
            projection = edge.navigation->project_point(agent.navmesh, {x, .1, 0});
            if (projection.status == NavStatus::Success && projection.points.size() == 1 &&
                x - projection.points.front()[0] > .1)
                break;
        }
        check(projection.status == NavStatus::Success && projection.points.size() == 1,
              "Missing eroded-edge projection fixture");
        const auto goal = projection.points.front();
        character.set<LocalTranslation>({goal[0] + .2, .1, goal[2]});
        agent.destination_x = goal[0];
        agent.destination_y = goal[1];
        agent.destination_z = goal[2];
        character.set<NavigationAgent>(agent);
        edge.tick();
        const auto ref = edge.engine.world().reference(character.id()).value();
        const auto observed = edge.navigation->agent_state(ref).value();
        const auto actual_gap =
            std::hypot(observed.position[0] - goal[0], observed.position[2] - goal[2]);
        if (observed.phase == NavigationAgentPhase::Arrived &&
            actual_gap > agent.stopping_distance + .001)
            throw std::runtime_error("Character arrived only at its mesh projection: " +
                                     edge.navigation->debug(character.id()).dump());
        check(observed.phase == NavigationAgentPhase::Moving &&
                  observed.remaining_distance > agent.stopping_distance && observed.velocity[0] < 0,
              "Character did not steer its actual centre toward the admitted edge goal");
        for (unsigned i = 0; i != 30; ++i)
            edge.tick();
        const auto arrived = edge.navigation->agent_state(ref).value();
        check(arrived.phase == NavigationAgentPhase::Arrived &&
                  std::hypot(arrived.position[0] - goal[0], arrived.position[2] - goal[2]) <=
                      agent.stopping_distance + .001,
              "Physical edge approach did not finish within the actual stopping distance");
    }
    for (unsigned invalid = 0; invalid != 6; ++invalid) {
        Fixture refused(root);
        refused.scene.restore_snapshot(configured);
        auto target = refused.scene.entity("agent");
        switch (invalid) {
        case 0: // Valid authored mode, unavailable physics service.
            break;
        case 1:
            target.remove<CharacterController>();
            break;
        case 2: {
            auto controller = target.get<CharacterController>();
            controller.enabled = false;
            target.set<CharacterController>(controller);
            break;
        }
        case 3:
            target.set<PhysicsBody>({});
            break;
        case 4:
            target.set<CharacterController2D>({});
            break;
        case 5:
            target.set<LocalRotation>({.70710678f, 0, 0, .70710678f});
            break;
        }
        const auto before = target.get<LocalTranslation>();
        refused.tick();
        const auto feedback = refused.navigation->debug(target.id());
        check(feedback.at("phase") == "Failed" && !feedback.at("sampled").get<bool>() &&
                  !feedback.at("diagnostic").get<std::string>().empty() &&
                  target.get<LocalTranslation>() == before,
              "Unsupported character movement configuration acquired navigation pose authority");
    }
    bool blocked = false;
    for (unsigned tick = 1; tick <= 180; ++tick) {
        run.tick();
        const auto observed = run.navigation->agent_state(reference).value();
        const auto actual = run.physics->character(reference);
        if (!observed.sampled || !observed.has_path)
            throw std::runtime_error("Character route lost its physical sample: " +
                                     run.navigation->debug(actor.id()).dump());
        check(observed.tick == tick && std::abs(observed.position[0] - actual.position.x) < 1e-6 &&
                  std::abs(observed.position[1] - actual.position.y) < 1e-6 &&
                  observed.velocity == actual.velocity,
              "Navigation published requested motion instead of actual adopted character state");
        check(actual.position.x < -7.3,
              "Navigation bypassed the physical wall or teleported its controller");
        blocked |= observed.phase == NavigationAgentPhase::Blocked;
    }
    check(blocked, "Physical character blockage never reached the navigation observation");
    const auto scene_image = run.scene.snapshot();
    const auto physics_image = run.physics->checkpoint();
    const auto navigation_image = run.navigation->checkpoint();
    const auto& saved_agent = navigation_image.at("agents").at(0);
    check(navigation_image.at("version").get<unsigned>() == 7 &&
              saved_agent.at("config").at("movement").get<unsigned>() == 1 &&
              saved_agent.at("translation").is_null() &&
              !saved_agent.at("translation_owned").get<bool>(),
          "Character recovery duplicated physics pose ownership or lost its movement profile");
    for (unsigned invalid = 0; invalid != 8; ++invalid) {
        auto bad = navigation_image;
        auto& entry = bad.at("agents").at(0);
        switch (invalid) {
        case 0:
            bad["version"] = 2;
            break;
        case 1:
            bad["version"] = 6;
            break;
        case 2:
            entry["translation"] = Json::array({-8, .1, 0});
            break;
        case 3:
            entry["translation_owned"] = true;
            break;
        case 4:
            entry["link"] = Json::object();
            break;
        case 5:
            entry["config"]["movement"] = 0;
            break;
        case 6:
            bad["version"] = 7.0;
            break;
        case 7:
            bad["version"] = std::uint64_t{0x100000007};
            break;
        }
        Fixture refused(root, true);
        refused.scene.restore_snapshot(scene_image);
        refused.physics->restore(physics_image);
        const auto before = refused.scene.snapshot();
        reject([&] { refused.navigation->restore(bad); });
        check(refused.scene.snapshot() == before,
              "Malformed character navigation recovery changed candidate Scene values");
    }
    Fixture recovered(root, true);
    recovered.scene.restore_snapshot(scene_image);
    recovered.physics->restore(physics_image);
    const auto restored_pose = recovered.scene.entity("agent").get<LocalTranslation>();
    recovered.navigation->restore(navigation_image);
    check(recovered.scene.entity("agent").get<LocalTranslation>() == restored_pose,
          "Navigation recovery wrote the character pose owned by physics");
    for (unsigned tick = 0; tick != 10; ++tick) {
        run.tick();
        recovered.tick();
        const auto actual = run.physics->character(reference);
        const auto resumed = recovered.physics->character(reference);
        check(std::hypot(actual.position.x - resumed.position.x,
                         actual.position.y - resumed.position.y,
                         actual.position.z - resumed.position.z) < .001 &&
                  resumed.position.x < -7.3,
              "Restored character navigation changed actual wall collision");
    }
    auto body = wall.get<PhysicsBody>();
    body.enabled = false;
    wall.set<PhysicsBody>(body);
    recovered.scene.entity(run.engine.world().reference(wall.id())->entity.str())
        .set<PhysicsBody>(body);
    bool arrived = false;
    for (unsigned tick = 0; tick != 240; ++tick) {
        run.tick();
        recovered.tick();
        const auto actual = run.physics->character(reference).position;
        const auto resumed = recovered.physics->character(reference).position;
        check(std::hypot(actual.x - resumed.x, actual.y - resumed.y, actual.z - resumed.z) < .001,
              "Recovered character route changed motion after the blocker was removed");
        if (run.navigation->agent_state(reference)->phase == NavigationAgentPhase::Arrived) {
            arrived = true;
            break;
        }
    }
    check(arrived && std::abs(run.physics->character(reference).position.x + 4) < .2,
          "Removing a physical blocker did not resume and finish the character route");
    const auto tune = [&](const char* field, const Json& value) {
        return run.navigation->tune(reference, "forge.navigation_agent", field, value);
    };
    reject([&] { tune("movement", 0); });
    tune("destination_x", -8);
    tune("speed", 0);
    const auto paused = run.physics->character(reference).position;
    run.tick();
    run.tick();
    check(std::abs(run.physics->character(reference).position.x - paused.x) < .0001 &&
              run.navigation->agent_state(reference)->phase == NavigationAgentPhase::Paused,
          "Live pause retained a character's autonomous one-step motion");
    tune("speed", 3);
    run.tick();
    check(run.physics->character(reference).position.x < paused.x - .01 &&
              run.navigation->agent_state(reference)->phase == NavigationAgentPhase::Moving,
          "Live retarget/resume did not steer the physical character");
    tune("has_destination", false);
    const auto idle = run.physics->character(reference).position;
    run.tick();
    run.tick();
    check(std::abs(run.physics->character(reference).position.x - idle.x) < .0001 &&
              run.navigation->agent_state(reference)->phase == NavigationAgentPhase::Idle,
          "Clearing the destination retained expired autonomous character movement");
    auto controller = actor.get<CharacterController>();
    controller.radius += .01f;
    actor.set<CharacterController>(controller);
    tune("has_destination", true);
    run.tick();
    check(run.physics->character(reference).position.x < idle.x - .01,
          "Controller shape recreation lost character navigation or retained expired intent");
    config = actor.get<NavigationAgent>();
    config.enabled = false;
    actor.set<NavigationAgent>(config);
    const auto before = run.physics->character(reference).position.z;
    run.physics->move_character(reference, {0, 0, .5});
    run.tick();
    run.tick();
    check(run.physics->character(reference).position.z > before + .01 &&
              run.navigation->agent_state(reference)->phase == NavigationAgentPhase::Disabled,
          "Disabled navigation retained autonomous intent or erased manual persistent movement");
    run.scene.delete_subtree(reference.entity.str());
    run.tick();
    check(!run.navigation->agent_state(reference),
          "Deleting a character retained a navigation observation");
    reject([&] { (void)run.physics->character(reference); });
}
