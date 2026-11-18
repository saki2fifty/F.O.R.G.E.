#pragma once
// Uses this test driver's existing project, Runtime and bounded refusal owners.
void retained_policy(const std::filesystem::path& root, const std::filesystem::path& worker,
                     NavigationPlane plane, bool dynamic) {
    SceneDocument::create_project(root, "Retained routing policy");
    EngineContext authoring;
    Scene scene(authoring.world());
    SceneDocument project(scene);
    project.open_project(root);
    const bool planar = plane == NavigationPlane::PlanarXY;
    std::string left;
    for (float x : {-3.f, 3.f}) {
        const auto id = create(scene, "Island");
        const auto e = scene.entity(id);
        e.set<LocalTranslation>({x, 0, 0});
        if (planar)
            e.set<NavigationRegion2D>({true, false, 4, 6});
        else
            e.set<Primitive>({3}).set<LocalScale>({4, 1, 6}).set<NavigationSurface>({});
        if (x < 0) {
            left = id;
            e.set<NavigationArea>({2});
            NavigationLink link;
            link.plane = unsigned(plane);
            link.start_x = planar ? 1.f : .25f;
            link.end_x = planar ? 5.f : 1.25f;
            e.set<NavigationLink>(link);
        }
    }
    NavigationSettings settings;
    settings.plane = plane;
    settings.dynamic_obstacles = dynamic;
    project.save();
    const auto baked = prepare_navigation(root, scene.effective_document(), settings, worker)
                           .publish(scene.effective_document());
    Query query(load(root, baked).mesh);
    NavigationAreaFilter initial, denied;
    initial.mask = 5;
    denied.mask = 1;
    const auto route = query.route({-3, 0, 0}, {3, 0, 0}, initial);
    check(route.path.status == NavStatus::Success && route.links.size() == 1,
          "Classified authored link did not connect both islands");
    const auto& link = route.links.front();
    check(query.link_available(EntityId::parse(left), route.path.points[link.entry],
                               route.path.points[link.exit], 0, initial) &&
              !query.link_available(EntityId::parse(left), route.path.points[link.entry],
                                    route.path.points[link.exit], 0, denied),
          "Retained link validation ignored connection/endpoint classes");
    const auto id = create(scene, "Crossing Agent");
    scene.entity(id)
        .set<LocalTranslation>({-3, 0, planar ? 7. : 0.})
        .set<NavigationAgent>({{baked.id}, true, true, 2, .05f, 3, 0, planar ? 7. : 0.})
        .set<NavigationAreaFilter>(initial);
    const auto authored = scene.snapshot();
    Runtime run(root, authored);
    const auto actor = run.scene.entity(id);
    const auto ref = run.engine.world().reference(actor.id()).value();
    bool crossing = false;
    for (unsigned i = 0; i < 600; ++i) {
        run.step();
        const auto debug = run.navigation->debug(actor.id());
        if (!debug.at("link").is_null() && actor.get<LocalTranslation>().x > -.5) {
            crossing = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(crossing, "Agent did not enter its classified connection");
    run.navigation->tune(ref, "forge.navigation_area_filter", "mask", 0);
    run.step();
    const auto checkpoint = run.navigation->checkpoint();
    const auto& entry = checkpoint.at("agents").at(0);
    check(checkpoint.at("version").get<unsigned>() == 13 &&
              entry.at("filter").at("config").at("mask").get<unsigned>() == 0 &&
              entry.at("link").at("filter").at("mask").get<unsigned>() == initial.mask,
          "Crossing policy edit did not retain its admission policy separately");
    Runtime recovered(root, authored);
    recovered.navigation->restore(checkpoint);
    const auto restored_actor = recovered.scene.entity(id);
    const auto restored_ref = recovered.engine.world().reference(restored_actor.id()).value();
    bool finished = false;
    for (unsigned i = 0; i < 240; ++i) {
        recovered.step();
        const auto debug = recovered.navigation->debug(restored_actor.id());
        const auto x = restored_actor.get<LocalTranslation>().x;
        check(x >= -.6, "Policy recovery teleported the crossing back to its source");
        if (recovered.navigation->agent_state(restored_ref)->phase ==
            NavigationAgentPhase::Failed) {
            check(debug.at("link").is_null() && x >= 1.9 && x <= 2.1,
                  "Edited policy took effect before the admitted crossing completed");
            finished = true;
            break;
        }
    }
    check(finished, "Completed crossing did not replan with the newly denied policy");
    for (unsigned damage = 0; damage < 4; ++damage) {
        auto invalid = checkpoint;
        auto& retained = invalid["agents"][0]["link"]["filter"];
        if (damage == 0)
            invalid["agents"][0]["link"].erase("filter");
        else if (damage == 1)
            retained["mask"] = 1;
        else if (damage == 2)
            retained["costs"][2] = 0;
        else
            invalid["version"] = 12;
        Runtime rejected(root, authored);
        rejects([&] { rejected.navigation->restore(invalid); });
    }
    std::cout << "Retained classified link policy and fresh recovery passed\n";
}
