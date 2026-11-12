#include <optional>
#pragma once
void planar_runtime_tests(const std::filesystem::path& root, Scene& authored, AssetId asset) {
    authored.entity("agent").set<LocalTranslation>({-8, 0, 37.125});
    authored.entity("agent").set<NavigationAgent>({{asset}, true, true, 4, .1f, 8, 0, -19});
    // This unrelated 3D source is invalid (zero-depth primitive); it cannot
    // invalidate the independently selected planar profile.
    authored.entity("floor").set<NavigationSurface>({true});
    const auto snapshot = authored.snapshot();
    navigation_live_tests(root, snapshot);
    navigation_feedback_tests(root, snapshot);
    Fixture run(root);
    run.scene.restore_snapshot(snapshot);
    const auto ref = run.engine.world().reference(run.scene.entity("agent").id()).value();
    const auto path = run.navigation->find_path({asset}, {-8, 0, 37.125}, {8, 0, -19});
    check(path.status == NavStatus::Success && path.points.size() > 2,
          "Runtime planar query lost its obstacle route or used invalid 3D source data");
    bool around = false;
    for (unsigned tick = 0; tick < 360; ++tick) {
        const auto before = run.position();
        run.tick();
        const auto point = run.position();
        const auto out = run.navigation->agent_state(ref).value();
        check(out.sampled && out.has_path && out.status == NavStatus::Success &&
                  point[2] == 37.125 && out.velocity[2] == 0 &&
                  out.projected_destination[2] == 37.125 &&
                  std::hypot(point[0] - before[0], point[1] - before[1]) <= 4. / 60 + .00001,
              "Planar agent lost world depth, movement budget or copied progress");
        around |= std::abs(point[1]) > 2.3;
        if (tick == 50) {
            const auto checkpoint = run.navigation->checkpoint();
            Fixture recovered(root);
            recovered.scene.restore_snapshot(run.scene.snapshot());
            recovered.navigation->restore(checkpoint);
            recovered.tick();
            run.tick();
            check(std::hypot(run.position()[0] - recovered.position()[0],
                             run.position()[1] - recovered.position()[1]) < .001 &&
                      recovered.position()[2] == 37.125,
                  "Fresh planar recovery changed route/motion or depth");
        }
    }
    check(around && run.navigation->agent_state(ref)->phase == NavigationAgentPhase::Arrived,
          "Planar agent did not route around the obstacle and arrive");
    {
        Fixture parented(root);
        parented.scene.restore_snapshot(snapshot);
        const auto parent_id =
            authoring_command(parented.scene, "entity.create", {{"name", "Spatial parent"}})
                .at("selected")
                .get<std::string>();
        auto parent = parented.scene.entity(parent_id);
        parent.set<LocalTranslation>({1, 2, 3});
        parent.set<LocalRotation>({0, .258819f, 0, .965926f});
        parent.set<LocalScale>({2, 1, 1});
        parented.engine.world().evaluate_world_transforms();
        auto agent = parented.scene.entity("agent");
        const auto local = inverse(parent.get<WorldTransform>().affine).point({-8, 0, 37.125});
        agent.set<SpatialBinding>({SpatialMode::Explicit, parented.scene.reference(parent_id)});
        agent.set<LocalTranslation>({local[0], local[1], local[2]});
        parented.tick();
        const auto position = parented.position();
        check(position[0] > -8 && std::abs(position[2] - 37.125) < 1e-8,
              "Planar world movement bypassed the existing inverse-parent transform owner");
    }
    run.scene.entity("obstacle").set<LocalTranslation>({0, 0, -123});
    check(run.navigation->project_point({asset}, {-8, 0, 19}).status == NavStatus::Success,
          "Runtime visual-depth-only source edit invalidated planar geometry");
    run.scene.entity("obstacle").set<NavigationRegion2D>({true, true, 3, 4});
    check(run.navigation->project_point({asset}, {-8, 0, 19}).status == NavStatus::Stale,
          "Runtime planar footprint edit did not invalidate the selected asset");
    run.scene.entity("obstacle").set<NavigationRegion2D>({true, true, 2, 4});
    check(run.navigation->project_point({asset}, {-8, 0, 19}).status == NavStatus::Success,
          "Restored planar footprint could not reuse unchanged asset geometry");
    // Both participants cross along logical Y at different visual depths.
    // The obstacle stays far from this crossing, keeping this an avoidance proof.
    Fixture avoiding(root);
    avoiding.scene.restore_snapshot(snapshot);
    auto a = avoiding.scene.entity("agent");
    const auto second = authoring_command(avoiding.scene, "entity.create", {{"name", "Other"}})
                            .at("selected")
                            .get<std::string>();
    auto b = avoiding.scene.entity(second);
    a.set<LocalTranslation>({-5, -4, 7});
    b.set<LocalTranslation>({-5, 4, -19});
    a.set<NavigationAgent>({{asset}, true, true, 2, .1f, -5, 4, -99});
    b.set<NavigationAgent>({{asset}, true, true, 2, .1f, -5, -4, 99});
    a.set<NavigationAvoidance>({});
    b.set<NavigationAvoidance>({});
    const auto a_ref = avoiding.engine.world().reference(a.id()).value();
    const auto b_ref = avoiding.engine.world().reference(b.id()).value();
    double minimum = 100;
    bool deviated = false;
    for (unsigned tick = 0; tick < 600; ++tick) {
        avoiding.tick();
        const auto aa = avoiding.navigation->agent_state(a_ref).value();
        const auto bb = avoiding.navigation->agent_state(b_ref).value();
        check(aa.sampled && bb.sampled && aa.position[2] == 7 && bb.position[2] == -19 &&
                  aa.velocity[2] == 0 && bb.velocity[2] == 0 &&
                  std::hypot(aa.velocity[0], aa.velocity[1]) <= 2.001,
              "Planar avoidance lost a movement sample, speed bound or visual depth");
        minimum = std::min(
            minimum, std::hypot(aa.position[0] - bb.position[0], aa.position[1] - bb.position[1]));
        deviated |= std::abs(aa.position[0] + 5) > .2;
    }
    check(deviated && minimum > .65 &&
              avoiding.navigation->agent_state(a_ref)->phase == NavigationAgentPhase::Arrived &&
              avoiding.navigation->agent_state(b_ref)->phase == NavigationAgentPhase::Arrived,
          "Planar participants did not avoid and arrive across logical Y");
    const auto destination = root.parent_path() / "planar-cooked";
    const std::array roots{asset};
    const RuntimePackageTarget target{"windows", "d3d12"};
    (void)package_runtime_content(root, destination, roots, target);
    const auto relocated = destination.string() + "-relocated";
    std::filesystem::rename(destination, relocated);
    const auto hidden = root.string() + "-hidden";
    std::filesystem::rename(root, hidden);
    const auto catalog = open_runtime_content(relocated, target);
    check(catalog.records().at(asset).schema_version == 3,
          "Cooked planar asset lost its envelope version");
    Fixture game(relocated);
    game.scene.restore_snapshot(snapshot);
    game.tick();
    check(game.position()[0] > -8 && game.position()[2] == 37.125,
          "Relocated planar navigation requires the source project or changes depth");
    std::filesystem::rename(hidden, root);
    std::cout
        << "Planar runtime/independent profiles/progress/recovery/avoidance/relocated cook PASS\n";
}
void planar_asset_tests(const std::filesystem::path& base, const std::filesystem::path& worker) {
    const auto root = base / "planar";
    std::filesystem::create_directories(root);
    EngineContext author;
    Scene scene(author.world());
    scene.restore_snapshot(input());
    const auto floor = scene.entity("floor");
    const auto obstacle = scene.entity("obstacle");
    floor.set<LocalScale>({1, 1, 0});
    floor.set<LocalTranslation>({0, 0, 7});
    floor.set<NavigationRegion2D>({true, false, 20, 20});
    obstacle.set<LocalScale>({1, 1, 0});
    obstacle.set<LocalTranslation>({0, 0, 19});
    obstacle.set<NavigationRegion2D>({true, true, 2, 4});
    NavigationSettings settings;
    settings.plane = NavigationPlane::PlanarXY;
    const auto prepare = [&] {
        return prepare_navigation(root, scene.effective_document(), settings, worker);
    };
    auto candidate = std::optional{prepare()};
    // Visual depth is not part of the navigation source geometry.
    floor.set<LocalTranslation>({0, 0, -7});
    const auto record = candidate->publish(scene.effective_document());
    check(record.schema_version == 3 && record.metadata.at("geometry_version") == 3 &&
              record.metadata.at("settings").at("plane") == 1 &&
              record.metadata.at("mesh_inputs").empty(),
          "Planar publication lost its guarded coordinate/provenance contract");
    check(AssetCatalog::open_project(root).resolve(AssetRef<NavMeshAsset>{record.id}).state ==
              AssetState::Available,
          "Planar asset is unavailable through the existing catalog");
    for (const auto& point : navigation_triangles(root, {record.id}))
        check(point[2] == 0, "Published planar triangles leaked native vertical height");
    const auto baseline = read(AssetCatalog::project_index(root));
    auto stale = std::optional{prepare()};
    obstacle.set<NavigationRegion2D>({true, true, 3, 4});
    reject([&] { stale->publish(scene.effective_document()); });
    check(read(AssetCatalog::project_index(root)) == baseline,
          "Stale planar candidate replaced the catalog");
    obstacle.set<NavigationRegion2D>({true, true, 2, 4});
    const auto staging = root / "worker-json";
    std::filesystem::create_directories(staging);
    const auto g = navigation_detail::geometry(
        navigation_detail::capture(scene.effective_document(), NavigationPlane::PlanarXY));
    auto metadata = record.metadata;
    metadata.erase("sha256");
    const auto request =
        Json{{"metadata", metadata}, {"vertices", g.vertices}, {"obstacles", g.obstacles}};
    const auto write_request = [&](const Json& value) {
        std::ofstream(staging / "source.json", std::ios::binary | std::ios::trunc) << value.dump();
    };
    const auto run = [&] {
        asset_detail::run_worker(asset_detail::WorkerKind::Navigation, worker, staging, {});
    };
    write_request(request);
    run();
    const auto retained = read(staging / "navigation.fnav");
    std::vector<Json> invalid;
    auto changed = request;
    changed.erase("obstacles");
    invalid.push_back(changed);
    changed = request;
    changed["obstacles"][0].push_back(0);
    invalid.push_back(changed);
    changed = request;
    changed["obstacles"][0][1] = 1;
    invalid.push_back(changed);
    changed = request;
    changed["vertices"][1] = 1;
    invalid.push_back(changed);
    for (unsigned version : {1u, 2u}) {
        changed = request;
        changed["metadata"]["version"] = version;
        invalid.push_back(changed);
    }
    changed = request;
    changed["metadata"]["settings"].erase("plane");
    invalid.push_back(changed);
    for (const auto& bad : invalid) {
        write_request(bad);
        reject(run);
        check(read(staging / "navigation.fnav") == retained &&
                  read(AssetCatalog::project_index(root)) == baseline,
              "Malformed planar request replaced retained output/catalog");
    }
    std::stop_source cancelled;
    cancelled.request_stop();
    reject([&] {
        prepare_navigation(root, scene.effective_document(), settings, worker,
                           cancelled.get_token());
    });
    check(read(AssetCatalog::project_index(root)) == baseline,
          "Cancelled planar request replaced catalog");
    const auto rebuilt = prepare().publish(scene.effective_document());
    check(rebuilt.id == record.id && rebuilt.source != record.source &&
              std::filesystem::is_regular_file(root / record.source),
          "Planar rebuild lost logical identity or the previous artifact");
    candidate.reset();
    stale.reset(); // End writer ownership before the runtime takes source files offline.
    planar_runtime_tests(root, scene, record.id);
    std::cout << "Planar worker/publication/staleness/cancellation/retained output PASS\n";
}
