#pragma once

void policy_values(const std::filesystem::path& root, const Json& snapshot) {
    Running run(root, snapshot);
    const auto id = run.scene.entity("a").get<PersistentEntityId>().value.str();
    const auto set = [&](const char* field, const Json& value) {
        authoring_command(run.scene, "property.set",
                          {{"entity", id},
                           {"component", "forge.navigation_avoidance"},
                           {"field", field},
                           {"value", value}});
    };
    authoring_command(run.scene, "component.add",
                      {{"entity", id}, {"component", "forge.navigation_avoidance"}});
    const auto baseline = run.scene.snapshot();
    const auto schema = run.scene.schema();
    unsigned found = 0;
    for (const auto& component : schema.at("components"))
        if (component.at("id").get<std::string>() == "forge.navigation_avoidance")
            for (const auto& field : component.at("fields")) {
                const auto key = field.at("id").get<std::string>();
                if (key != "layers" && key != "mask" && key != "priority")
                    continue;
                check(!field.at("animatable").get<bool>() &&
                          field.at("minimum").get<double>() == 0 &&
                          field.at("maximum").get<double>() ==
                              (key == "priority" ? 1. : double(UINT32_MAX)),
                      ("Avoidance policy discovery lost native bounds: " + field.dump()).c_str());
                ++found;
            }
    check(found == 3, "Avoidance discovery omitted a policy field");
    const auto& builtins = detail::builtins();
    const auto owner = std::ranges::find_if(builtins, [](const auto& type) {
        return std::string_view(type.name) == "forge.navigation_avoidance";
    });
    check(owner != builtins.end(), "Avoidance source owner is missing");
    const Json legacy_value{{"enabled", true}, {"horizon", 2}, {"shared_group", 0}};
    check(detail::canonical_builtin_value(*owner, legacy_value) == legacy_value,
          "Default policy changed the legacy canonical builtin value");
    auto legacy_source = baseline;
    for (auto& row : legacy_source["entities"])
        if (row.at("id").get<std::string>() == id)
            row["components"]["forge.navigation_avoidance"] = {{"enabled", true}, {"horizon", 2}};
    Running legacy_reopened(root, legacy_source);
    check(legacy_reopened.scene.entity(id).get<NavigationAvoidance>() == NavigationAvoidance{},
          "Older authored avoidance source lost current defaults");
    for (const auto* field : {"layers", "mask"})
        for (const auto& invalid : {Json(-1), Json(std::uint64_t(UINT32_MAX) + 1), Json(.5),
                                    Json(true), Json("mask"), Json(nullptr)}) {
            reject([&] { set(field, invalid); });
            check(run.scene.snapshot() == baseline, "Invalid policy mask changed authored state");
        }
    for (const auto& invalid :
         {Json(-.1), Json(1.1), Json(true), Json("priority"), Json(nullptr)}) {
        reject([&] { set("priority", invalid); });
        check(run.scene.snapshot() == baseline, "Invalid priority changed authored state");
    }
    set("layers", 0x80000000u);
    set("mask", 0x80000000u);
    set("priority", .75);
    const auto authored = run.scene.snapshot();
    check(run.scene.entity(id).get<NavigationAvoidance>().layers == 0x80000000u &&
              run.scene.entity(id).get<NavigationAvoidance>().mask == 0x80000000u &&
              run.scene.entity(id).get<NavigationAvoidance>().priority == .75f &&
              run.scene.undo() && run.scene.entity(id).get<NavigationAvoidance>().priority == .5f &&
              run.scene.redo() && run.scene.snapshot() == authored,
          "Native high bits/priority did not retain exact values and ordinary history");
    Running reopened(root, authored);
    check(reopened.scene.entity(id).get<NavigationAvoidance>() ==
              run.scene.entity(id).get<NavigationAvoidance>(),
          "Reopen changed avoidance policy");
    const auto ref = run.engine.world().reference(run.scene.entity(id).id()).value();
    check(run.navigation->tune_avoidance(ref, "mask", 0).get<std::uint32_t>() == 0 &&
              run.navigation->tune_avoidance(ref, "priority", 1).get<float>() == 1 &&
              run.navigation->tune_avoidance(ref, "layers", UINT32_MAX).get<std::uint32_t>() ==
                  UINT32_MAX,
          "Live policy edits did not use the existing tune owner");
    reject([&] { run.navigation->tune_avoidance(ref, "priority", 1.1); });
    reject([&] { run.navigation->tune_avoidance(ref, "layers", -.5); });
    run.tick();
    const auto checkpoint = run.navigation->checkpoint();
    check(checkpoint.at("version").get<unsigned>() == 14, "Policy recovery profile is missing");
    Running restored(root, run.scene.snapshot());
    restored.navigation->restore(checkpoint);
    check(restored.scene.entity(id).get<NavigationAvoidance>() ==
              run.scene.entity(id).get<NavigationAvoidance>(),
          "Recovery lost the live policy");
    auto invalid = checkpoint;
    invalid["version"] = 2;
    const auto untouched = restored.scene.snapshot();
    reject([&] { restored.navigation->restore(invalid); });
    check(restored.scene.snapshot() == untouched, "Downgrade partially published recovery");
    for (auto& row : invalid["agents"])
        if (!row.at("avoidance").is_null())
            row["avoidance"]["config"]["priority"] = -.1;
    invalid["version"] = 14;
    reject([&] { restored.navigation->restore(invalid); });
    check(restored.scene.snapshot() == untouched, "Malformed policy partially published recovery");
    Running ordinary(root, baseline);
    ordinary.tick();
    auto old = ordinary.navigation->checkpoint();
    check(old.at("version").get<unsigned>() == 2, "Default policy replaced legacy recovery2");
    old["version"] = 14;
    old["behaviors"] = Json::array();
    reject([&] { ordinary.navigation->restore(old); });
    const auto ordinary_ref =
        ordinary.engine.world().reference(ordinary.scene.entity(id).id()).value();
    check(
        ordinary.navigation->tune_avoidance(ordinary_ref, "layers", 1).get<std::uint32_t>() == 1 &&
            ordinary.navigation->tune_avoidance(ordinary_ref, "mask", UINT32_MAX)
                    .get<std::uint32_t>() == UINT32_MAX &&
            ordinary.navigation->tune_avoidance(ordinary_ref, "priority", .5).get<float>() == .5f &&
            ordinary.navigation->checkpoint().at("version").get<unsigned>() == 2,
        "Optional default tuning failed or changed the legacy recovery profile");

    NavigationAreaFilter area;
    area.costs[2] = 20;
    run.scene.entity(id).set<NavigationAreaFilter>(area);
    run.tick();
    const auto combined = run.navigation->checkpoint();
    check(combined.at("version").get<unsigned>() == 14, "Mixed policy recovery lost its profile");
    Running mixed(root, run.scene.snapshot());
    mixed.navigation->restore(combined);
    check(mixed.scene.entity(id).get<NavigationAreaFilter>() == area &&
              mixed.scene.entity(id).get<NavigationAvoidance>() ==
                  run.scene.entity(id).get<NavigationAvoidance>(),
          "Mixed recovery lost an independent area or peer policy");
    auto downgraded = combined;
    downgraded["version"] = 13;
    const auto mixed_before = mixed.scene.snapshot();
    reject([&] { mixed.navigation->restore(downgraded); });
    check(mixed.scene.snapshot() == mixed_before, "Mixed policy downgrade changed native state");

    auto entity = run.scene.entity(id);
    auto typed = entity.get<NavigationAvoidance>();
    typed.priority = std::numeric_limits<float>::quiet_NaN();
    const auto before = entity.get<LocalTranslation>();
    entity.set<NavigationAvoidance>(typed);
    run.tick();
    check(run.state("a").status == NavStatus::Invalid && entity.get<LocalTranslation>() == before,
          "Nonfinite native priority reached movement");
}

void policy_routes(const std::filesystem::path& root, const Json& snapshot, bool planar) {
    struct Case {
        std::uint32_t mask, layers;
        float own_priority, peer_priority;
        bool selected;
    };
    for (const auto& test :
         {Case{UINT32_MAX, 1, .5f, .5f, true}, Case{0, 1, .5f, .5f, false},
          Case{UINT32_MAX, 0, .5f, .5f, false}, Case{0x80000000u, 0x80000000u, .5f, .5f, true},
          Case{1, 0x80000000u, .5f, .5f, false}, Case{UINT32_MAX, 1, .8f, .2f, false},
          Case{UINT32_MAX, 1, .2f, .8f, true}}) {
        Running actual(root, snapshot), isolated(root, snapshot);
        for (auto* run : {&actual, &isolated}) {
            auto a = run->scene.entity("a"), b = run->scene.entity("b");
            auto config = b.get<NavigationAgent>();
            config.has_destination = false;
            b.set<NavigationAgent>(config).set<LocalTranslation>({0, .1, planar ? 7. : 0.});
            if (planar)
                a.set<LocalTranslation>({-4, .1, 2});
            NavigationAvoidance own;
            own.mask = test.mask;
            own.priority = test.own_priority;
            a.set<NavigationAvoidance>(own);
            NavigationAvoidance peer;
            peer.layers = test.layers;
            peer.priority = test.peer_priority;
            peer.enabled = run == &actual;
            b.set<NavigationAvoidance>(peer);
        }
        double deviation = 0, minimum = 100;
        for (unsigned tick = 0; tick != 360; ++tick) {
            actual.tick();
            isolated.tick();
            const auto state = actual.state("a");
            check(state.sampled && state.status == NavStatus::Success &&
                      isolated.state("a").status == NavStatus::Success,
                  "Policy route lost an admitted native sample");
            const auto position = actual.position("a"), peer = actual.position("b");
            deviation = std::max(deviation, std::abs(position[planar ? 1 : 2] - (planar ? .1 : 0)));
            minimum =
                std::min(minimum, std::hypot(position[0] - peer[0],
                                             position[planar ? 1 : 2] - peer[planar ? 1 : 2]));
            if (!test.selected)
                check(position == isolated.position("a"), "Excluded peer changed actual steering");
            if (planar)
                check(position[2] == 2 && peer[2] == 7, "Peer policy changed independent XY depth");
        }
        check(actual.state("a").phase == NavigationAgentPhase::Arrived &&
                  (test.selected ? deviation > .2 && minimum > .65
                                 : deviation < .001 && minimum < .1),
              "Mask/priority did not select the expected actual avoidance route");
    }
}

void yielding_pair(const std::filesystem::path& root, const Json& snapshot, bool planar) {
    Running actual(root, snapshot);
    auto a = actual.scene.entity("a"), b = actual.scene.entity("b");
    NavigationAvoidance dominant, yielding;
    dominant.priority = .8f;
    yielding.priority = .2f;
    a.set<NavigationAvoidance>(dominant);
    b.set<NavigationAvoidance>(yielding);
    if (planar) {
        a.set<LocalTranslation>({-4, .1, 2});
        b.set<LocalTranslation>({4, .1, 7});
    }
    double minimum = 100, yield_deviation = 0;
    for (unsigned tick = 0; tick != 360; ++tick) {
        actual.tick();
        check(actual.state("a").status == NavStatus::Success &&
                  actual.state("b").status == NavStatus::Success,
              "Actual yielding pair lost navigation admission");
        const auto first = actual.position("a"), second = actual.position("b");
        check(std::abs(first[planar ? 1 : 2] - (planar ? .1 : 0)) < .001,
              "Higher-priority moving agent reacted to a lower-priority peer");
        yield_deviation =
            std::max(yield_deviation, std::abs(second[planar ? 1 : 2] - (planar ? .1 : 0)));
        minimum = std::min(minimum, std::hypot(first[0] - second[0],
                                               first[planar ? 1 : 2] - second[planar ? 1 : 2]));
    }
    check(actual.state("a").phase == NavigationAgentPhase::Arrived &&
              actual.state("b").phase == NavigationAgentPhase::Arrived && yield_deviation > .2 &&
              minimum > .65,
          "The lower-priority moving agent did not yield and arrive without the observed overlap");
}

Json prepare_policy_scene(const std::filesystem::path& root, const std::filesystem::path& worker,
                          bool planar) {
    std::filesystem::create_directories(root);
    EngineContext author;
    Scene scene(author.world());
    scene.restore_snapshot(initial_scene());
    NavigationSettings settings;
    if (planar) {
        auto floor = scene.entity("floor");
        floor.remove<NavigationSurface>();
        floor.set<LocalScale>({1, 1, 1}).set<LocalTranslation>({0, 0, 0});
        NavigationRegion2D region;
        region.width = region.height = 20;
        floor.set<NavigationRegion2D>(region);
        settings.plane = NavigationPlane::PlanarXY;
    }
    auto candidate = prepare_navigation(root, scene.effective_document(), settings, worker);
    const auto record = candidate.publish(scene.effective_document());
    scene.entity("a").set<NavigationAgent>({{record.id}, true, true, 2, .1f, 4, .1, 0});
    scene.entity("b").set<NavigationAgent>({{record.id}, true, true, 2, .1f, -4, .1, 0});
    return scene.snapshot();
}
void planar_policies(const std::filesystem::path& root, const std::filesystem::path& worker) {
    const auto snapshot = prepare_policy_scene(root, worker, true);
    policy_routes(root, snapshot, true);
    yielding_pair(root, snapshot, true);
}
void prepare_policy_sdk(const std::filesystem::path& scratch, const std::filesystem::path& worker,
                        bool planar) {
    const auto root = scratch / AssetId::generate().str();
    EngineContext author;
    Scene scene(author.world());
    scene.restore_snapshot(prepare_policy_scene(root, worker, planar));
    for (const auto* name : {"a", "b"}) {
        NavigationAvoidance policy;
        policy.layers = policy.mask = 0x80000000u;
        const bool first = std::string_view(name) == "a";
        policy.priority = first ? .8f : .2f;
        auto entity = scene.entity(name);
        entity.set<NavigationAvoidance>(policy);
        if (planar)
            entity.set<LocalTranslation>({first ? -4. : 4., .1, first ? 2. : 7.});
    }
    std::ofstream source(root / "scene.json");
    source << scene.snapshot().dump();
    source.close();
    check(bool(source), "Avoidance SDK Scene fixture write failed");
    std::ofstream project(root / "forge.project.json");
    project << Json{{"version", 2},
                    {"name", "Avoidance policies"},
                    {"startup_scene", nullptr},
                    {"input", {{"version", 1}, {"actions", Json::array()}}},
                    {"modules", Json::array()}}
                   .dump();
    project.close();
    check(bool(project), "Avoidance SDK project fixture write failed");
    std::cout << root.string() << '\n';
}
