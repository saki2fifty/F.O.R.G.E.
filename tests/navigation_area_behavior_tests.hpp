#pragma once
void behavior_policies(const std::filesystem::path& root, const std::filesystem::path& worker,
                       NavigationPlane plane, bool peers = false) {
    exercise(root, worker, plane, true, true);
    std::ifstream input(root / "scene.json");
    Json authored;
    input >> authored;
    const auto lease = std::make_shared<ProjectLease>(root);
    for (bool tree : {false, true}) {
        const auto doc = NavigationBehaviorDocument::create(
            lease, tree ? "Assets/AreaTree.behavior.json" : "Assets/AreaGraph.behavior.json",
            [&](AssetId id) {
                auto value = tree ? NavigationBehaviorSource::create_tree(id)
                                  : NavigationBehaviorSource::create(id);
                const Json point = {7, plane == NavigationPlane::PlanarXY ? 0 : .1, 0};
                if (tree)
                    value.document["nodes"] = Json::array({{{"id", 1},
                                                            {"name", "Cross areas"},
                                                            {"kind", "move_to_point"},
                                                            {"point", point}}});
                else {
                    value.document["states"][0]["name"] = "Cross areas";
                    value.document["states"][0]["action"] = {{"kind", "move_to_point"},
                                                             {"point", point}};
                }
                return value;
            });
        AssetImportService imports(lease, navigation_behavior_import_registry(),
                                   {"windows", "d3d12", "desktop"}, 1);
        imports.submit(
            imports.prepare(doc->locator(), {}, doc->source().asset()),
            [](auto& candidate, const auto& plan, const auto&) {
                prepare_navigation_behavior_publication(candidate, plan);
            },
            [](const auto&, const auto&) {});
        check(imports.wait_idle(std::chrono::seconds(10)), "Area behavior publication timed out");
        const auto outcomes = imports.poll();
        check(outcomes.size() == 1 && outcomes.front().published,
              "Area behavior asset did not publish");
        EngineContext author;
        Scene source(author.world());
        source.restore_snapshot(authored);
        std::string actor;
        for (const auto& row : authored.at("entities"))
            if (row.at("name").get<std::string>() == "Filtered agent")
                actor = row.at("id").get<std::string>();
        check(!actor.empty(), "Missing behavior policy actor");
        source.entity(actor).set<NavigationBehavior>({{doc->source().asset()}, {}, {}, true});
        std::string peer;
        if (peers) {
            NavigationAvoidance policy;
            policy.layers = policy.mask = 4;
            policy.priority = .75f;
            source.entity(actor).set<NavigationAvoidance>(policy);
            for (const auto& row : authored.at("entities"))
                if (row.at("name").get<std::string>() == "Direct agent")
                    peer = row.at("id").get<std::string>();
            check(!peer.empty(), "Missing behavior avoidance peer");
            auto idle = source.entity(peer);
            auto agent = idle.get<NavigationAgent>();
            agent.has_destination = false;
            policy.priority = 1;
            idle.set<NavigationAgent>(agent).set<NavigationAvoidance>(policy).set<LocalTranslation>(
                {0, plane == NavigationPlane::PlanarXY ? 0. : .1,
                 plane == NavigationPlane::PlanarXY ? 11. : 0.});
        }
        Runtime run(root, source.snapshot(), false, true);
        const auto native = run.scene.entity(actor);
        const auto ref = run.engine.world().reference(native.id()).value();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        for (;;) {
            run.engine.services().resources()->synchronize();
            run.step();
            if (native.has<NavigationBehaviorCursor>() &&
                run.navigation->agent_state(ref)->has_path)
                break;
            check(std::chrono::steady_clock::now() < deadline,
                  "Area behavior routing readiness timed out");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(native.get<NavigationAreaFilter>().costs[2] == 20 &&
                  run.navigation->debug(native.id()).at("path").size() > 2,
              "Behavior-owned destination bypassed the Agent's route policy");
        auto costs = Json::array();
        for (unsigned i = 0; i < 16; ++i)
            costs.push_back(1);
        run.navigation->tune(ref, "forge.navigation_area_filter", "costs", costs);
        run.step();
        check(run.navigation->debug(native.id()).at("path").size() == 2 &&
                  native.get<NavigationBehavior>().enabled,
              "Live cost edit did not replan behavior movement through the existing owner");
        if (peers) {
            double minimum = 100;
            bool passed = false;
            const auto planar = plane == NavigationPlane::PlanarXY;
            for (unsigned i = 0; i < 600; ++i) {
                run.step();
                run.engine.world().evaluate_world_transforms();
                const auto position = native.get<WorldTransform>().affine.point({0, 0, 0});
                const auto other =
                    run.scene.entity(peer).get<WorldTransform>().affine.point({0, 0, 0});
                check(run.navigation->agent_state(ref)->status == NavStatus::Success &&
                          native.get<NavigationBehavior>().enabled,
                      "Behavior destination lost admitted peer-policy steering");
                minimum =
                    std::min(minimum, std::hypot(position[0] - other[0],
                                                 position[planar ? 1 : 2] - other[planar ? 1 : 2]));
                if (position[0] > 1) {
                    passed = true;
                    break;
                }
            }
            check(passed && minimum > .65,
                  "Behavior-owned destination did not avoid its selected higher-priority peer");
        }
        const auto checkpoint = run.navigation->checkpoint();
        check(checkpoint.is_object() &&
                  checkpoint.at("version").get<unsigned>() == (peers ? 14u : 13u) &&
                  !checkpoint.at("behaviors").empty(),
              "Configured area policy dropped behavior recovery state");
        std::cout << (tree ? "Tree" : "State graph") << " filtered destination policy passed\n";
    }
}
