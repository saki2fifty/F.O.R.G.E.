// Shared fixture helpers are declared by physics_tests.cpp.
void ancestry_rejected(Fixture& f, flecs::entity child, flecs::entity ancestor,
                       const std::function<void()>& operation) {
    const auto before = f.scene.snapshot();
    const auto status = f.physics->status();
    try {
        operation();
    } catch (const PhysicsConfigurationError& error) {
        const auto diagnostic = diagnostic_json(error.diagnostic);
        const auto child_ref = *f.engine.world().reference(child);
        check(diagnostic.at("category") == "physics.unsupported_dynamic_ancestry",
              "Ancestry diagnostic category");
        check(diagnostic.at("context").at("entity") == Json(child_ref.entity) &&
                  diagnostic.at("context").at("asset") == Json(child_ref.scene),
              "Ancestry diagnostic lost child identity");
        check(diagnostic.at("context").at("related_entity") ==
                  Json(*f.engine.world().reference(ancestor)),
              "Ancestry diagnostic lost Dynamic ancestor identity");
        check(f.scene.snapshot() == before, "Rejected ancestry rewrote authored state");
        check(f.physics->status() == status, "Rejected ancestry advanced/changed physics");
        check(!f.engine.services().diagnostics().empty(), "Missing structured service diagnostic");
        return;
    }
    throw std::runtime_error("Unsupported Dynamic spatial ancestry accepted");
}
void setup_hierarchy(Fixture& f, unsigned motion, bool intermediary = false) {
    auto doc = source();
    doc["entities"] =
        Json::array({body("parent", 10, 2), body("child", 2, motion), body("middle", 0, 0)});
    f.scene.reset(doc);
    auto parent = f.scene.entity("parent"), child = f.scene.entity("child"),
         middle = f.scene.entity("middle");
    middle.remove<PhysicsBody>().remove<BoxCollider>();
    middle.child_of(parent).set<SpatialBinding>({SpatialMode::FollowStructure, {}});
    child.child_of(intermediary ? middle : parent);
    child.set<SpatialBinding>({SpatialMode::FollowStructure, {}});
    child.set<LocalTranslation>({3, 2, 0});
}
void physics_hierarchy_tests() {
    for (unsigned motion : {0u, 1u}) {
        for (bool intermediary : {false, true}) {
            Fixture f;
            setup_hierarchy(f, motion, intermediary);
            // Reproduces the former ~8 cm divergence fixture. Reject before its first step.
            ancestry_rejected(f, f.scene.entity("child"), f.scene.entity("parent"),
                              [&] { f.tick(30); });
            check(f.physics->status().at("tick") == 0 && f.physics->status().at("bodies") == 0,
                  "Invalid ancestry reached simulation");
        }
        {
            Fixture f;
            setup_hierarchy(f, motion, true);
            auto child = f.scene.entity("child"), middle = f.scene.entity("middle"),
                 parent = f.scene.entity("parent");
            child.remove(flecs::ChildOf, flecs::Wildcard);
            middle.remove(flecs::ChildOf, flecs::Wildcard);
            child.set<SpatialBinding>({SpatialMode::Explicit, *f.engine.world().reference(middle)});
            middle.set<SpatialBinding>(
                {SpatialMode::Explicit, *f.engine.world().reference(parent)});
            ancestry_rejected(f, child, parent, [&] { f.physics->synchronize(0); });
            middle.set<SpatialBinding>({SpatialMode::World, {}});
            f.physics->synchronize(0);
            f.tick(30);
            check(std::abs(child.get<WorldTransform>().affine.m[7] - 2) < 1e-6,
                  "World intermediary failed to break Explicit ancestry");
        }
        {
            Fixture f;
            setup_hierarchy(f, motion);
            auto child = f.scene.entity("child"), parent = f.scene.entity("parent");
            child.set<SpatialBinding>({SpatialMode::World, {}});
            f.physics->synchronize(0);
            f.tick(30);
            check(child.target(flecs::ChildOf) == parent &&
                      std::abs(child.get<WorldTransform>().affine.m[7] - 2) < 1e-6,
                  "World body lost structural ownership/independence");
            auto snapshot = f.scene.snapshot(), checkpoint = f.physics->checkpoint();
            Fixture restored;
            restored.scene.restore_snapshot(snapshot);
            restored.physics->restore(checkpoint);
            restored.tick();
            f.tick();
            check(std::abs(y(f, "parent") - y(restored, "parent")) < 1e-7,
                  "Valid physics hierarchy recovery changed motion");
            Fixture invalid;
            invalid.scene.restore_snapshot(snapshot);
            auto c = invalid.scene.entity(f.scene.canonical_id("child"));
            auto p = invalid.scene.entity(f.scene.canonical_id("parent"));
            c.set<SpatialBinding>({SpatialMode::FollowStructure, {}});
            ancestry_rejected(invalid, c, p, [&] { invalid.physics->restore(checkpoint); });
            // Existing-body spatial changes are checked again at every synchronization.
            child.set<SpatialBinding>({SpatialMode::FollowStructure, {}});
            ancestry_rejected(f, child, parent, [&] { f.physics->synchronize(1.f / 60); });
        }
        for (unsigned parent_motion : {0u, 1u}) {
            Fixture f;
            setup_hierarchy(f, motion, true);
            auto parent = f.scene.entity("parent"), child = f.scene.entity("child");
            parent.set<PhysicsBody>({parent_motion});
            f.physics->synchronize(0);
            auto ref = *f.engine.world().reference(parent);
            if (parent_motion == 1)
                f.physics->move_kinematic(ref, {0, 12, 0}, {});
            else
                f.physics->teleport(ref, {0, 12, 0}, {}, true);
            f.tick();
            auto world = decompose(child.get<WorldTransform>().affine).translation;
            auto hit = f.physics->raycast({world.x, world.y + 3, world.z}, {0, -6, 0});
            check(hit && hit->entity == *f.engine.world().reference(child) &&
                      std::abs(hit->position[1] - .5 - world.y) < 1e-5 &&
                      std::abs(world.y - 14) < 1e-5,
                  "Parent command was not synchronized before child collision target");
            // A component change on an ancestor must not bypass the restriction.
            parent.set<PhysicsBody>({2});
            ancestry_rejected(f, child, parent, [&] { f.physics->synchronize(1.f / 60); });
        }
    }
    {
        Fixture f;
        setup_hierarchy(f, 0, true);
        auto child = f.scene.entity("child");
        child.remove<PhysicsBody>().remove<BoxCollider>();
        f.physics->synchronize(0);
        f.tick(30);
        check(std::abs(child.get<WorldTransform>().affine.m[7] - y(f, "parent") - 2) < 1e-7,
              "Visual descendants no longer follow simulated parent");
        child.set<PhysicsBody>({0}).set<BoxCollider>({});
        ancestry_rejected(f, child, f.scene.entity("parent"),
                          [&] { f.physics->synchronize(1.f / 60); });
    }
    // A completed checkpoint must not mix a post-physics transform edit with old solver state.
    // Include a visual intermediary: its late movement can move a separate descendant body.
    for (unsigned motion : {0u, 1u}) {
        Fixture f;
        setup_hierarchy(f, motion, true);
        auto parent = f.scene.entity("parent");
        parent.remove<PhysicsBody>().remove<BoxCollider>();
        f.physics->synchronize(0);
        f.tick();
        (void)f.physics->checkpoint();
        f.scene.entity("middle").set<LocalTranslation>({0, 1, 0});
        reject([&] { (void)f.physics->checkpoint(); });
        f.tick();
        (void)f.physics->checkpoint();
    }
    // Target preflight must preserve both authored values and component ownership on failure.
    for (bool teleport : {false, true}) {
        Fixture f;
        setup_hierarchy(f, 1);
        auto parent = f.scene.entity("parent"), child = f.scene.entity("child");
        parent.remove<PhysicsBody>().remove<BoxCollider>();
        parent.set<LocalScale>({2, 1, 1});
        f.physics->synchronize(0);
        const auto before = f.scene.snapshot();
        auto ref = *f.engine.world().reference(child);
        const auto world = decompose(child.get<WorldTransform>().affine);
        if (teleport)
            f.physics->teleport(ref, world.translation, rotation_about_axis({0, 0, 1}, 90), true);
        else
            f.physics->move_kinematic(ref, world.translation, rotation_about_axis({0, 0, 1}, 90));
        try {
            f.physics->synchronize(1.f / 60);
            throw std::runtime_error("Scale-changing target accepted");
        } catch (const std::exception& e) {
            check(std::string(e.what()).find("LocalScale") != std::string::npos,
                  "Missing scaled-target diagnostic");
        }
        check(f.scene.snapshot() == before, "Rejected target mutated local channels/ownership");
        auto hit =
            f.physics->raycast({world.translation.x, world.translation.y + 3, 0}, {0, -6, 0});
        check(hit && std::abs(hit->position[1] - .5 - world.translation.y) < 1e-5,
              "Rejected target moved Jolt body");
    }
    // Supported commands use the updated parent target, in FIFO order, with unchanged scale.
    {
        Fixture f;
        setup_hierarchy(f, 1);
        auto parent = f.scene.entity("parent"), child = f.scene.entity("child");
        parent.set<PhysicsBody>({1});
        f.physics->synchronize(0);
        f.physics->move_kinematic(*f.engine.world().reference(parent), {0, 12, 0}, {});
        f.physics->move_kinematic(*f.engine.world().reference(child), {3, 15, 0}, {});
        f.tick();
        check(std::abs(child.get<LocalTranslation>().y - 3) < 1e-5 &&
                  std::abs(child.get<WorldTransform>().affine.m[7] - 15) < 1e-5,
              "Child command used stale parent transform");
    }
    // Structured prefab interiors use the same effective spatial ancestry and validation.
    for (unsigned motion : {0u, 1u}) {
        Fixture source;
        setup_hierarchy(source, motion);
        source.scene.entity("child").set<LocalScale>({1, 1, 1}).set<LocalRotation>({});
        auto prefab = create_prefab_source(source.scene, source.scene.canonical_id("parent"));
        source.scene.set_prefab_sources({{prefab.asset(), prefab.source}});
        source.scene.delete_subtree(source.scene.canonical_id("parent"));
        auto root = instantiate_prefab(source.scene, prefab.asset());
        source.scene.entity(root).set<SpatialBinding>({SpatialMode::World, {}});
        std::string child;
        const auto doc = source.scene.document();
        for (const auto& entity : doc.at("entities"))
            if (entity.at("id") == root)
                for (const auto& [member, id] : entity.at("prefab_instance").at("members").items())
                    if (source.scene.entity(id.get<std::string>()).has<PhysicsBody>() && id != root)
                        child = id.get<std::string>();
        check(!child.empty(), "Prefab body member missing");
        Fixture invalid;
        invalid.scene.restore_snapshot(source.scene.snapshot());
        ancestry_rejected(invalid, invalid.scene.entity(child), invalid.scene.entity(root),
                          [&] { invalid.physics->synchronize(0); });
        // Reconcile a valid Static root into Dynamic, retaining the instance's World binding.
        auto valid = prefab.source;
        for (auto& member : valid["members"])
            if (member["id"] == prefab.source.at("root"))
                member["components"]["forge.physics_body"]["motion"] = 0;
        valid["revision"] = 2;
        source.scene.publish_prefab_sources({{prefab.asset(), valid}}, [] {});
        source.physics->synchronize(0);
        auto changed = prefab.source;
        changed["revision"] = 3;
        source.scene.publish_prefab_sources({{prefab.asset(), changed}}, [] {});
        ancestry_rejected(source, source.scene.entity(child), source.scene.entity(root),
                          [&] { source.physics->synchronize(1.f / 60); });
        source.scene.entity(child).set<SpatialBinding>({SpatialMode::World, {}});
        source.physics->synchronize(0);
        auto child_entity = source.scene.entity(child);
        check(!child_entity.owns<LocalScale>() && !child_entity.owns<LocalRotation>(),
              "Prefab transform inheritance fixture");
        const auto pose = decompose(child_entity.get<WorldTransform>().affine);
        source.physics->teleport(*source.engine.world().reference(child_entity),
                                 {pose.translation.x + 1, pose.translation.y, pose.translation.z},
                                 pose.rotation, true);
        source.tick();
        check(!child_entity.owns<LocalScale>() && !child_entity.owns<LocalRotation>(),
              "Translation target created unrelated prefab overrides");
    }
}
