#pragma once
#include <limits>

void navigation_grounded_planar_character_tests(const std::filesystem::path& root,
                                                const Json& source) {
    const auto add_box = [](Fixture& run, const char* name, double x, double y, float width,
                            float height) {
        const auto id = authoring_command(run.scene, "entity.create", {{"name", name}})
                            .at("selected")
                            .get<std::string>();
        const auto entity = run.scene.entity(id);
        entity.set<SpatialBinding>({SpatialMode::World, {}});
        entity.set<LocalTranslation>({x, y, 91});
        entity.set<PhysicsBody2D>({});
        entity.set<BoxCollider2D>({width, height});
        return entity;
    };
    const auto prepare = [&](Fixture& run) {
        run.scene.restore_snapshot(source);
        const auto actor = run.scene.entity("agent");
        actor.set<SpatialBinding>({SpatialMode::World, {}});
        CharacterController2D controller;
        controller.radius = .1f;
        controller.height = .05f;
        controller.gravity_factor = 1;
        actor.set<CharacterController2D>(controller);
        auto agent = actor.get<NavigationAgent>();
        agent.movement = 3;
        agent.destination_x = -4;
        agent.destination_y = 0;
        actor.set<NavigationAgent>(agent);
        return actor;
    };
    Fixture run(root, true);
    const auto actor = prepare(run);
    add_box(run, "Physical grounded floor", 0, -.5, 20, 1);
    const auto wall = add_box(run, "Physical grounded wall", -6, 2, .2f, 4);
    const auto reference = run.engine.world().reference(actor.id()).value();
    bool blocked = false;
    for (unsigned i = 0; i < 100; ++i) {
        run.tick();
        const auto actual = run.physics->character_2d(reference);
        const auto state = run.navigation->agent_state(reference).value();
        if (!(state.sampled && state.has_path && state.position[0] == actual.position[0] &&
              state.position[1] == actual.position[1] && state.position[2] == 37.125 &&
              state.velocity == Double3{actual.velocity[0], actual.velocity[1], 0} &&
              actual.position[0] < -6.15))
            throw std::runtime_error("Grounded XY physical observation failed at tick " +
                                     std::to_string(i) + ": " +
                                     run.navigation->debug(actor.id()).dump());
        blocked |= state.phase == NavigationAgentPhase::Blocked;
    }
    check(blocked, "A grounded physical wall never produced blocked feedback");
    const auto saved = run.navigation->checkpoint();
    check(saved.at("version") == 10 && saved.at("agents")[0].at("translation").is_null(),
          "Grounded XY recovery did not retain explicit physics pose ownership");
    Fixture recovered(root, true);
    recovered.scene.restore_snapshot(run.scene.snapshot());
    recovered.physics->restore(run.physics->checkpoint());
    recovered.navigation->restore(saved);
    auto downgraded = saved;
    downgraded["version"] = 8;
    reject([&] { recovered.navigation->restore(downgraded); });
    recovered.tick();
    wall.remove<PhysicsBody2D>();
    for (unsigned i = 0; i < 150; ++i)
        run.tick();
    auto state = run.navigation->agent_state(reference).value();
    check(state.phase == NavigationAgentPhase::Arrived &&
              run.physics->character_2d(reference).ground == CharacterGround::OnGround &&
              std::abs(state.position[0] + 4) <= .101,
          "Grounded XY routing failed to resume and physically arrive after wall removal");
    auto exact = actor.get<NavigationAgent>();
    exact.stopping_distance = .01f;
    actor.set<NavigationAgent>(exact);
    for (unsigned i = 0; i < 30; ++i)
        run.tick();
    state = run.navigation->agent_state(reference).value();
    if (!(state.phase == NavigationAgentPhase::Arrived &&
          state.remaining_distance <= character_contact_band_2d + .001))
        throw std::runtime_error("Minimum-stop grounded contact admission failed: " +
                                 run.navigation->debug(actor.id()).dump());
    run.physics->move_character_2d(reference, {0, 4});
    run.tick();
    check(run.physics->character_2d(reference).ground == CharacterGround::InAir &&
              run.navigation->agent_state(reference)->phase != NavigationAgentPhase::Arrived,
          "An airborne manual jump was reported as grounded arrival");
    auto agent = actor.get<NavigationAgent>();
    agent.has_destination = false;
    actor.set<NavigationAgent>(agent);
    run.tick();
    const auto rising = run.physics->character_2d(reference);
    for (unsigned i = 0; i < 80; ++i)
        run.tick();
    check(rising.velocity[1] < 4 &&
              run.physics->character_2d(reference).ground == CharacterGround::OnGround,
          "Cancelling grounded navigation froze native vertical gravity");
    Fixture gap(root, true);
    const auto gap_actor = prepare(gap);
    add_box(gap, "Left static ground", -8, -.5, 4, 1);
    add_box(gap, "Right static ground", 3, -.5, 14, 1);
    agent = gap_actor.get<NavigationAgent>();
    agent.destination_x = -2;
    gap_actor.set<NavigationAgent>(agent);
    const auto gap_ref = gap.engine.world().reference(gap_actor.id()).value();
    for (unsigned i = 0; i < 180; ++i)
        gap.tick();
    state = gap.navigation->agent_state(gap_ref).value();
    check(state.phase == NavigationAgentPhase::Blocked && state.position[0] < -5.99 &&
              gap.physics->character_2d(gap_ref).ground == CharacterGround::OnGround,
          "Grounded XY navigation crossed unsupported space or invented arrival");
    Fixture detour(root, true);
    const auto detour_actor = prepare(detour);
    add_box(detour, "Static floor below unsupported route bend", 0, -.5, 20, 1);
    agent = detour_actor.get<NavigationAgent>();
    agent.destination_x = 4; // The baked XY obstacle requires an airborne bend.
    detour_actor.set<NavigationAgent>(agent);
    for (unsigned i = 0; i < 3; ++i)
        detour.tick();
    check(detour.navigation->debug(detour_actor.id()).at("phase") == "Failed" &&
              detour_actor.get<LocalTranslation>().x == -8,
          "Grounded navigation approached a route bend with no physical support");
    for (bool invalid_layer : {false, true}) {
        Fixture invalid(root, true);
        const auto invalid_actor = prepare(invalid);
        add_box(invalid, "Static ground for invalid controller", 0, -.5, 20, 1);
        invalid.tick();
        const auto before = invalid_actor.get<LocalTranslation>();
        auto controller = invalid_actor.get<CharacterController2D>();
        if (invalid_layer)
            controller.layer = 32;
        else
            controller.gravity_factor = std::numeric_limits<float>::quiet_NaN();
        invalid_actor.set<CharacterController2D>(controller);
        reject([&] { invalid.tick(); });
        check(invalid_actor.get<LocalTranslation>() == before,
              "Invalid grounded controller advanced a partial physics pose");
    }
    Fixture vertical(root, true);
    const auto vertical_actor = prepare(vertical);
    add_box(vertical, "Static ground below unreachable goal", 0, -.5, 20, 1);
    agent = vertical_actor.get<NavigationAgent>();
    agent.destination_y = 3;
    vertical_actor.set<NavigationAgent>(agent);
    for (unsigned i = 0; i < 3; ++i)
        vertical.tick();
    const auto failed = vertical.navigation->debug(vertical_actor.id());
    check(failed.at("phase") == "Failed" &&
              failed.at("diagnostic").get<std::string>().find("static walkable support") !=
                  std::string::npos,
          "Grounded XY routing admitted an unsupported vertical destination");
}
