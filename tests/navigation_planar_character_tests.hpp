#pragma once

void navigation_planar_character_tests(const std::filesystem::path& root, const Json& source) {
    Fixture run(root, true);
    run.scene.restore_snapshot(source);
    const auto actor = run.scene.entity("agent");
    actor.set<SpatialBinding>({SpatialMode::World, {}});
    CharacterController2D controller;
    controller.mode = 1;
    controller.radius = .1f;
    controller.height = .05f;
    actor.set<CharacterController2D>(controller);
    auto agent = actor.get<NavigationAgent>();
    agent.movement = 2;
    agent.destination_x = -4; // A head-on approach distinguishes blockage from wall sliding.
    actor.set<NavigationAgent>(agent);
    const auto reference = run.engine.world().reference(actor.id()).value();
    const auto configured = run.scene.snapshot();
    {
        EngineContext author;
        Scene definitions(author.world());
        definitions.restore_snapshot(configured);
        const auto prefab = create_prefab_source(definitions, "agent");
        definitions.set_prefab_sources({{prefab.asset(), prefab.source}});
        const auto instance = instantiate_prefab(definitions, prefab.asset());
        check(!definitions.entity(instance).owns<NavigationAgent>() &&
                  definitions.entity(instance).get<NavigationAgent>().movement == 2,
              "Floating XY movement did not inherit through the existing Prefab owner");
        authoring_command(definitions, "property.set",
                          {{"entity", instance},
                           {"component", "forge.navigation_agent"},
                           {"field", "movement"},
                           {"value", 0}});
        check(definitions.entity(instance).owns<NavigationAgent>() &&
                  definitions.entity(instance).get<NavigationAgent>().movement == 0 &&
                  definitions.undo() &&
                  definitions.entity(instance).get<NavigationAgent>().movement == 2 &&
                  definitions.redo() &&
                  definitions.entity(instance).get<NavigationAgent>().movement == 0,
              "XY movement Prefab override lost canonical zero or Undo/Redo");
        EngineContext reloader;
        Scene reloaded(reloader.world());
        reloaded.restore_snapshot(definitions.snapshot());
        check(reloaded.entity(instance).get<NavigationAgent>().movement == 0,
              "XY movement Prefab override did not round-trip through Scene serialization");
    }
    for (unsigned invalid = 0; invalid != 4; ++invalid) {
        Fixture refused(root);
        refused.scene.restore_snapshot(configured);
        const auto entity = refused.scene.entity("agent");
        if (invalid == 0)
            entity.remove<CharacterController2D>();
        else if (invalid == 1) {
            auto value = controller;
            value.mode = 0;
            entity.set<CharacterController2D>(value);
        } else if (invalid == 2)
            entity.set<PhysicsBody2D>({});
        const auto before = entity.get<LocalTranslation>();
        refused.tick();
        const auto out = refused.navigation->debug(entity.id());
        check(out.at("phase") == "Failed" && !out.at("sampled").get<bool>() &&
                  entity.get<LocalTranslation>() == before,
              "Unsupported XY character profile acquired navigation pose authority");
    }
    const auto obstacle = run.scene.entity("obstacle");
    obstacle.set<SpatialBinding>({SpatialMode::World, {}});
    obstacle.set<PhysicsBody2D>({});
    obstacle.set<BoxCollider2D>({2, 4});
    const auto wall_id = authoring_command(run.scene, "entity.create", {{"name", "Physical wall"}})
                             .at("selected")
                             .get<std::string>();
    const auto wall = run.scene.entity(wall_id);
    wall.set<SpatialBinding>({SpatialMode::World, {}});
    wall.set<LocalTranslation>({-6, 0, 100});
    wall.set<PhysicsBody2D>({});
    wall.set<BoxCollider2D>({.2f, 20});
    bool blocked = false;
    for (unsigned i = 0; i != 120; ++i) {
        run.tick();
        const auto actual = run.physics->character_2d(reference);
        const auto out = run.navigation->agent_state(reference).value();
        check(out.sampled && out.has_path && out.position[0] == actual.position[0] &&
                  out.position[1] == actual.position[1] && out.position[2] == 37.125 &&
                  out.velocity == Double3{actual.velocity[0], actual.velocity[1], 0} &&
                  out.projected_destination[2] == 37.125 && actual.position[0] < -6.19,
              "XY navigation published requested motion or bypassed physical collision");
        blocked |= out.phase == NavigationAgentPhase::Blocked;
    }
    if (!blocked)
        throw std::runtime_error("XY character wall never produced actual blocked feedback: " +
                                 run.navigation->debug(actor.id()).dump());
    const auto image = run.navigation->checkpoint();
    check(image.at("version").get<unsigned>() == 8 &&
              image.at("agents").at(0).at("translation").is_null(),
          "XY navigation recovery duplicated physics pose authority");
    for (unsigned invalid = 0; invalid != 4; ++invalid) {
        auto bad = image;
        if (invalid < 2)
            bad["version"] = invalid ? 7 : 6;
        else if (invalid == 2)
            bad["agents"][0]["translation"] = Json::array({-8, 0, 37.125});
        else
            bad["agents"][0]["config"]["movement"] = 1;
        Fixture refused(root, true);
        refused.scene.restore_snapshot(run.scene.snapshot());
        refused.physics->restore(run.physics->checkpoint());
        const auto before = refused.scene.snapshot();
        reject([&] { refused.navigation->restore(bad); });
        check(refused.scene.snapshot() == before,
              "Malformed XY navigation recovery changed the unpublished Scene");
    }
    Fixture copy(root, true);
    copy.scene.restore_snapshot(run.scene.snapshot());
    copy.physics->restore(run.physics->checkpoint());
    copy.navigation->restore(image);
    const auto wall_ref = run.engine.world().reference(wall.id()).value();
    for (auto* fixture : {&run, &copy}) {
        auto body = fixture->scene.entity(wall_ref.entity.str()).get<PhysicsBody2D>();
        body.enabled = false;
        fixture->scene.entity(wall_ref.entity.str()).set<PhysicsBody2D>(body);
        fixture->navigation->tune(reference, "forge.navigation_agent", "destination_x", 8);
    }
    bool arrived = false, around = false;
    for (unsigned i = 0; i != 420; ++i) {
        run.tick();
        copy.tick();
        const auto actual = run.physics->character_2d(reference);
        const auto resumed = copy.physics->character_2d(reference);
        const auto out = run.navigation->agent_state(reference).value();
        check(out.sampled && out.has_path && out.position[0] == actual.position[0] &&
                  out.position[1] == actual.position[1] && out.position[2] == 37.125 &&
                  out.velocity == Double3{actual.velocity[0], actual.velocity[1], 0} &&
                  actual.position == resumed.position && actual.velocity == resumed.velocity,
              "XY character feedback or restored physical route diverged");
        around |= std::abs(actual.position[1]) > 2.3;
        if (out.phase == NavigationAgentPhase::Arrived) {
            arrived = true;
            check(std::hypot(actual.position[0] - out.projected_destination[0],
                             actual.position[1] - out.projected_destination[1]) <=
                      agent.stopping_distance + .001,
                  "XY character arrived from route projection instead of actual pose");
            break;
        }
    }
    check(arrived && around, "XY character failed to resume, follow corners and arrive");
    run.navigation->tune(reference, "forge.navigation_agent", "destination_x", -8);
    run.navigation->tune(reference, "forge.navigation_agent", "speed", 0);
    const auto paused = run.physics->character_2d(reference).position;
    run.tick();
    run.tick();
    check(run.physics->character_2d(reference).position == paused &&
              run.navigation->agent_state(reference)->phase == NavigationAgentPhase::Paused,
          "Live XY pause retained autonomous intent");
    reject([&] { run.navigation->tune(reference, "forge.navigation_agent", "movement", 0); });
    run.navigation->tune(reference, "forge.navigation_agent", "enabled", false);
    run.physics->move_character_2d(reference, {-1, 1});
    run.tick();
    run.tick();
    check(run.physics->character_2d(reference).velocity == Double2{-1, 1},
          "Disabled XY navigation erased manual persistent motion");
    std::cout << "XY character actual collision/corners/arrival/recovery/live ownership PASS\n";
}
