#pragma once

// Existing publication, GameSession, resource admission and physics owners.
void continuous_collision_replacement(const std::filesystem::path& root, CollisionSource& source,
                                      AssetImportService& service) {
    auto& node = source.document["nodes"][0];
    const auto path = root / "Assets/test.collision.json";
    node["scale"] = {2, 1, 1};
    save(path, source);
    require(import(service, source).published, "Positive CCD collision fixture failed publication");
    runtime_collision(root, source.asset(), [&](GameSession& game) {
        auto physics = game.active().physics();
        auto entity = game.active().scene.entity("collision");
        auto settings = entity.get<PhysicsBody>();
        settings.motion = 2;
        settings.gravity_factor = 0;
        settings.continuous_collision = true;
        entity.set<PhysicsBody>(settings);
        physics->synchronize(0);
        const auto before = physics->checkpoint();
        const auto old_hit = physics->raycast({-5, 0, 0}, {10, 0, 0});
        require(old_hit.has_value(), "Positive CCD cooked shape was not realized");
        node["scale"] = {-2, 1, 1};
        save(path, source);
        require(import(service, source).published,
                "Mirrored candidate should cook for discrete collision consumers");
        physics->refresh_assets();
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        bool rejected = false;
        while (!rejected) {
            physics->prepare_assets();
            physics->synchronize(0);
            const auto status = physics->status();
            for (const auto& resource : status.at("collision_resources"))
                rejected |= resource.at("diagnostic")
                                .get<std::string>()
                                .find("positive native inner radius") != std::string::npos;
            require(std::chrono::steady_clock::now() < deadline,
                    "CCD-incompatible live asset did not report its rejection: " + status.dump());
            std::this_thread::sleep_for(1ms);
        }
        const auto retained_hit = physics->raycast({-5, 0, 0}, {10, 0, 0});
        require(physics->checkpoint() == before && retained_hit &&
                    retained_hit->fraction == old_hit->fraction,
                "Rejected CCD resource replacement changed native state or geometry");
        node["scale"] = {3, 1, 1};
        save(path, source);
        require(import(service, source).published, "Compatible CCD replacement failed publication");
        const auto revision = AssetCatalog::open_project(root)
                                  .records()
                                  .at(source.asset())
                                  .metadata.at("forge.import")
                                  .at("key");
        physics->refresh_assets();
        const auto replacement_deadline = std::chrono::steady_clock::now() + 10s;
        while (physics->status().at("collision_resources")[0].at("revision") != revision) {
            physics->prepare_assets();
            physics->synchronize(0);
            require(std::chrono::steady_clock::now() < replacement_deadline,
                    "Compatible CCD resource replacement did not reach the boundary");
            std::this_thread::sleep_for(1ms);
        }
        const auto replaced_hit = physics->raycast({-5, 0, 0}, {10, 0, 0});
        require(replaced_hit && replaced_hit->fraction < old_hit->fraction &&
                    entity.get<PhysicsBody>().continuous_collision,
                "Compatible CCD replacement lost geometry or authored setting");
    });
    // Subsequent source-free/relocated discrete consumers still exercise mirrors.
    node["scale"] = {-3, 1, 1};
    save(path, source);
    require(import(service, source).published, "Discrete mirror fixture restoration failed");
}
