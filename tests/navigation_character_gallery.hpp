#pragma once

constexpr const char* character_scene_path = "Scenes/224-navigation-characters.scene.json";
constexpr const char* planar_character_scene_path =
    "Scenes/225-navigation-characters-xy.scene.json";
void prepare_character_gallery(const std::filesystem::path& root,
                               const std::filesystem::path& worker, bool planar = false) {
    const auto* path = planar ? planar_character_scene_path : character_scene_path;
    const auto lease = std::make_shared<ProjectLease>(root);
    EngineContext engine;
    Scene scene(engine.world());
    scene.load(root / path);
    const auto effective = scene.effective_document();
    NavigationSettings settings;
    if (planar)
        settings.plane = NavigationPlane::PlanarXY;
    const auto nav = prepare_navigation(root, effective, settings, worker).publish(effective).id;
    engine.world().world().each([&](flecs::entity e, const NavigationAgent& source) {
        auto agent = source;
        agent.navmesh = {nav};
        e.set<NavigationAgent>(agent);
    });
    scene.save(root / path);
    const std::array roots{scene.asset_id()};
    prepare_runtime_content_catalog(*lease, roots, {}, scene.schema());
}
void exercise_character_gallery(const std::filesystem::path& root,
                                const std::filesystem::path& source, bool planar = false) {
    const auto saved = read(root / source);
    GameSession session(session_config(root));
    const auto candidate = session.prepare(saved);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!session.poll_preparation(candidate).ready) {
        check(std::chrono::steady_clock::now() < deadline, "Character Gallery readiness timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    session.activate(candidate, {}, false);
    auto& world = session.active();
    auto navigation =
        std::static_pointer_cast<NavigationRuntime>(world.engine.services().navigation());
    const auto physics = world.engine.services().physics();
    flecs::entity free, blocked, wall;
    for (const auto& row : saved.at("entities")) {
        const auto name = row.at("name").get<std::string>();
        if (name == "Free route character")
            free = world.scene.entity(row.at("id"));
        else if (name == "Blocked route character")
            blocked = world.scene.entity(row.at("id"));
        else if (name.starts_with("Red physical wall"))
            wall = world.scene.entity(row.at("id"));
    }
    check(free && blocked && wall && physics, "Character Gallery participants are missing");
    const auto ref = [&](flecs::entity e) {
        return world.scene.reference(e.get<PersistentEntityId>().value.str());
    };
    const auto actual = [&](flecs::entity e) -> std::pair<Double3, Double3> {
        if (planar) {
            const auto value = physics->character_2d(ref(e));
            return {{value.position[0], value.position[1], e.get<LocalTranslation>().z},
                    {value.velocity[0], value.velocity[1], 0}};
        }
        const auto value = physics->character(ref(e));
        return {{value.position.x, value.position.y, value.position.z}, value.velocity};
    };
    bool saw_blocked = false;
    for (unsigned tick = 0; tick != 420; ++tick) {
        session.step();
        for (const auto actor : {free, blocked}) {
            const auto state = navigation->agent_state(ref(actor));
            const auto [position, velocity] = actual(actor);
            check(state && state->sampled && std::abs(state->position[0] - position[0]) < 1e-6 &&
                      std::abs(state->position[1] - position[1]) < 1e-6 &&
                      state->position[2] == position[2] && state->velocity == velocity,
                  "Character Gallery failed actual physics feedback: " +
                      navigation->debug(actor.id()).dump());
        }
        const auto state = navigation->agent_state(ref(blocked));
        saw_blocked |= state->phase == NavigationAgentPhase::Blocked;
        check(actual(blocked).first[0] < (planar ? -.44 : -.45),
              "Gallery character bypassed its physical wall");
    }
    check(saw_blocked && navigation->agent_state(ref(free))->phase == NavigationAgentPhase::Arrived,
          "Gallery did not demonstrate both physical blockage and free arrival");
    if (planar) {
        auto body = wall.get<PhysicsBody2D>();
        body.enabled = false;
        wall.set<PhysicsBody2D>(body);
    } else {
        auto body = wall.get<PhysicsBody>();
        body.enabled = false;
        wall.set<PhysicsBody>(body);
    }
    for (unsigned tick = 0; tick != 240; ++tick) {
        session.step();
        if (navigation->agent_state(ref(blocked))->phase == NavigationAgentPhase::Arrived)
            return;
    }
    throw std::runtime_error("Gallery character did not resume after disabling its wall");
}
