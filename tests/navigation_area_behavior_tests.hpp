#pragma once
void behavior_policies(const std::filesystem::path& root, const std::filesystem::path& worker,
                       NavigationPlane plane) {
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
        const auto checkpoint = run.navigation->checkpoint();
        check(checkpoint.is_object() && checkpoint.at("version").get<unsigned>() == 13 &&
                  !checkpoint.at("behaviors").empty(),
              "Configured area policy dropped behavior recovery state");
        std::cout << (tree ? "Tree" : "State graph") << " filtered destination policy passed\n";
    }
}
