#pragma once
void physical_policies(const std::filesystem::path& root, const std::filesystem::path& worker,
                       NavigationPlane plane) {
    exercise(root, worker, plane, true, true);
    std::ifstream input(root / "scene.json");
    Json authored;
    input >> authored;
    EngineContext author;
    Scene source(author.world());
    source.restore_snapshot(authored);
    std::string actor;
    for (const auto& row : authored.at("entities"))
        if (row.at("name").get<std::string>() == "Filtered agent")
            actor = row.at("id").get<std::string>();
    check(!actor.empty(), "Missing filtered physical actor");
    const auto planar = plane == NavigationPlane::PlanarXY;
    auto entity = source.entity(actor);
    entity.set<SpatialBinding>({SpatialMode::World, {}});
    auto configuration = entity.get<NavigationAgent>();
    configuration.movement = planar ? 2 : 1;
    entity.set<NavigationAgent>(configuration);
    if (planar) {
        CharacterController2D controller;
        controller.mode = 1;
        controller.radius = .1f;
        controller.height = .05f;
        entity.set<CharacterController2D>(controller);
    } else {
        entity.set<CharacterController>({});
        const auto ground = source.entity(create(source, "Physical support"));
        ground.set<SpatialBinding>({SpatialMode::World, {}})
            .set<LocalTranslation>({0, -.5, 0})
            .set<PhysicsBody>({})
            .set<BoxCollider>({18, 1, 12});
    }
    Runtime run(root, source.snapshot(), true);
    const auto native = run.scene.entity(actor);
    const auto ref = run.engine.world().reference(native.id()).value();
    double extent = 0;
    bool followed = false;
    for (unsigned i = 0; i < 600; ++i) {
        run.step();
        run.engine.world().evaluate_world_transforms();
        const auto position = native.get<WorldTransform>().affine.point({0, 0, 0});
        extent = std::max(extent, std::abs(position[planar ? 1 : 2]));
        const auto status = run.navigation->agent_state(ref).value();
        if (status.phase == NavigationAgentPhase::Failed)
            throw std::runtime_error(
                "Physical routing policy failed at " + Json(position).dump() + " projection " +
                Json(run.navigation->project_point(configuration.navmesh, position).points).dump() +
                ": " + run.navigation->debug(native.id()).dump());
        if (planar)
            check(position[2] == 7, "Filtered Floating XY movement changed visual depth");
        if (position[0] > 1 && extent > 2.8) {
            followed = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(followed, "Native physical character did not follow the cheaper classified route");
    const auto checkpoint = run.navigation->checkpoint();
    const auto found = std::ranges::find_if(checkpoint.at("agents"), [&](const auto& row) {
        return row.at("entity").template get<EntityRef>().entity.str() == actor;
    });
    check(found != checkpoint.at("agents").end() && found->at("translation").is_null() &&
              !found->at("translation_owned").template get<bool>(),
          "Filtered character recovery introduced navigation pose ownership");
    run.navigation->tune(ref, "forge.navigation_area_filter", "mask", 0);
    run.step();
    check(run.navigation->agent_state(ref)->phase == NavigationAgentPhase::Failed,
          "Physical character retained routing permission after mask removal");
    run.navigation->tune(ref, "forge.navigation_area_filter", "mask", 65535);
    auto costs = Json::array();
    for (unsigned i = 0; i < 16; ++i)
        costs.push_back(1);
    run.navigation->tune(ref, "forge.navigation_area_filter", "costs", costs);
    run.step();
    check(run.navigation->agent_state(ref)->phase == NavigationAgentPhase::Moving &&
              run.navigation->debug(native.id()).at("path").size() == 2,
          "Native character did not resume with the newly cheaper direct route");
    std::cout << (planar ? "XY floating" : "3D physical") << " filtered character policy passed\n";
}
