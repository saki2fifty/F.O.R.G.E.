#pragma once
#include <limits>
// Reuses the actual navigation fixture and the same saved inputs for each plane.
inline void navigation_live_tests(const std::filesystem::path& root, const Json& source) {
    Fixture run(root);
    run.scene.restore_snapshot(source);
    auto actor = run.scene.entity("agent");
    const auto ref = run.engine.world().reference(actor.id()).value();
    const auto original = actor.get<NavigationAgent>();
    const auto tune = [&](const char* field, const Json& value) {
        return run.navigation->tune(ref, "forge.navigation_agent", field, value);
    };
    run.tick();
    const auto pose = run.position();
    const auto tick = run.navigation->agent_state(ref)->tick;
    check(tune("speed", 0) == 0 && run.position() == pose &&
              run.navigation->agent_state(ref)->tick == 0,
          "Live speed advanced movement or retained a stale configuration sample");
    run.tick();
    check(run.position() == pose &&
              run.navigation->agent_state(ref)->phase == NavigationAgentPhase::Paused,
          "Live zero speed did not pause the individual agent");
    tune("speed", 3);
    run.tick();
    check(run.position() != pose && run.navigation->agent_state(ref)->tick > tick &&
              run.navigation->agent_state(ref)->phase == NavigationAgentPhase::Moving,
          "Live speed did not resume the ordinary fixed-tick agent");
    const auto moved = run.position();
    tune("has_destination", false);
    run.tick();
    check(run.position() == moved &&
              run.navigation->agent_state(ref)->phase == NavigationAgentPhase::Idle,
          "Clearing a live destination did not stop route following");
    tune("destination_x", -8);
    tune("destination_y", original.destination_y);
    tune("destination_z", original.destination_z);
    tune("has_destination", true);
    run.tick();
    check(run.navigation->agent_state(ref)->projected_destination[0] < -7.9,
          "Live retarget did not invalidate the existing route");
    tune("enabled", false);
    const auto disabled = run.position();
    run.tick();
    check(run.position() == disabled &&
              run.navigation->agent_state(ref)->phase == NavigationAgentPhase::Disabled,
          "Live disable retained movement authority");
    tune("enabled", true);
    tune("stopping_distance", .25);
    tune("destination_x", 8);
    const auto configured = actor.get<NavigationAgent>();
    const auto unchanged = run.scene.snapshot();
    for (const auto& [field, value] : std::vector<std::pair<const char*, Json>>{
             {"speed", -1},
             {"speed", 21},
             {"speed", "fast"},
             {"speed", true},
             {"speed", std::numeric_limits<double>::infinity()},
             {"enabled", 1},
             {"has_destination", "true"},
             {"stopping_distance", 0},
             {"stopping_distance", 6},
             {"destination_x", 4091},
             {"destination_y", -4091},
             {"destination_z", nullptr},
             {"navmesh", nullptr},
             {"width", 3}}) {
        reject([&] { tune(field, value); });
        check(actor.get<NavigationAgent>() == configured && run.scene.snapshot() == unchanged,
              "Rejected live agent candidate changed runtime values");
    }
    reject([&] { run.navigation->tune(ref, "forge.navigation_region_2d", "width", 3); });
    auto foreign = ref;
    foreign.scene = AssetId::generate();
    reject([&] { run.navigation->tune(foreign, "forge.navigation_agent", "speed", 4); });
    auto missing = ref;
    missing.entity = EntityId::generate();
    reject([&] { run.navigation->tune(missing, "forge.navigation_agent", "speed", 4); });
    bool denied = false;
    std::thread other([&] {
        try {
            tune("speed", 4);
        } catch (const std::exception&) {
            denied = true;
        }
    });
    other.join();
    check(denied && actor.get<NavigationAgent>() == configured,
          "Foreign thread acquired live navigation mutation authority");
    ecs_readonly_begin(run.engine.world().world().c_ptr(), false);
    reject([&] { tune("speed", 4); });
    ecs_readonly_end(run.engine.world().world().c_ptr());
    const auto floor = run.engine.world().reference(run.scene.entity("floor").id()).value();
    reject([&] { run.navigation->tune(floor, "forge.navigation_agent", "speed", 4); });
    const auto checkpoint = run.navigation->checkpoint();
    Fixture resumed(root);
    resumed.scene.restore_snapshot(run.scene.snapshot());
    resumed.navigation->restore(checkpoint);
    check(resumed.scene.entity("agent").get<NavigationAgent>() == configured,
          "Live agent configuration failed fresh semantic recovery");
    run.tick();
    resumed.tick();
    check(std::hypot(run.position()[0] - resumed.position()[0],
                     run.position()[1] - resumed.position()[1],
                     run.position()[2] - resumed.position()[2]) < .001,
          "Recovered live agent target changed the next movement result");
    EngineContext author;
    Scene definitions(author.world());
    definitions.restore_snapshot(source);
    NavigationRuntime author_access(author.world(), root);
    author_access.bind(&definitions);
    const auto authored_ref = author.world().reference(definitions.entity("agent").id()).value();
    reject([&] { author_access.tune(authored_ref, "forge.navigation_agent", "speed", 4); });
    const auto prefab = create_prefab_source(definitions, "agent");
    definitions.set_prefab_sources({{prefab.asset(), prefab.source}});
    const auto instance = instantiate_prefab(definitions, prefab.asset());
    for (const bool has_override : {false, true}) {
        if (has_override)
            authoring_command(definitions, "property.set",
                              {{"entity", instance},
                               {"component", "forge.navigation_agent"},
                               {"field", "speed"},
                               {"value", 4}});
        const auto authored_before = definitions.snapshot();
        Fixture inherited(root);
        inherited.scene.restore_snapshot(authored_before);
        auto live_instance = inherited.scene.entity(instance);
        check(live_instance.owns<NavigationAgent>() == has_override,
              "Live inherited fixture has unexpected property ownership");
        const auto inherited_ref = inherited.engine.world().reference(live_instance.id()).value();
        inherited.navigation->tune(inherited_ref, "forge.navigation_agent", "speed", 6);
        check(live_instance.get<NavigationAgent>().speed == 6 &&
                  definitions.snapshot() == authored_before &&
                  inherited.scene.prefab_sources() == definitions.prefab_sources(),
              "Live inherited agent edit changed its authored source or Prefab definition");
        const auto effective = inherited.scene.effective_document();
        const auto row =
            std::find_if(effective.at("entities").begin(), effective.at("entities").end(),
                         [&](const auto& candidate) { return candidate.at("id") == instance; });
        check(row != effective.at("entities").end() &&
                  row->at("components").at("forge.navigation_agent").at("speed") == 6,
              "Live snapshot masked the runtime value behind authored Prefab intent");
        const auto inherited_checkpoint = inherited.navigation->checkpoint();
        Fixture recovered_instance(root);
        recovered_instance.scene.restore_snapshot(inherited.scene.snapshot());
        recovered_instance.navigation->restore(inherited_checkpoint);
        check(recovered_instance.scene.entity(instance).get<NavigationAgent>().speed == 6,
              "Fresh recovery lost a temporary inherited agent configuration");
    }
    std::cout << "Live agent shared validation, pause/retarget, authority and recovery passed\n";
}
