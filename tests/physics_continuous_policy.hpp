#pragma once

// Uses the same public runtime fixture as the discrete/continuous comparison.
void continuous_collision_policy_tests() {
    const auto fast_source = [] {
        auto wall = body("wall", 1.5, 0), moving = body("fast", 3, 2);
        wall["components"]["forge.box_collider"] = {{"x", 4}, {"y", .02}, {"z", 4}};
        moving["components"].erase("forge.box_collider");
        moving["components"]["forge.sphere_collider"] = {{"radius", .05}};
        moving["components"]["forge.physics_body"]["continuous_collision"] = true;
        return Json{{"version", 1}, {"entities", Json::array({wall, moving})}};
    };
    auto fast_config = PhysicsConfig{};
    fast_config.gravity = {0, -10000, 0};
    for (unsigned policy = 0; policy < 4; ++policy) {
        Fixture f;
        f.physics->configure(fast_config);
        auto input = fast_source();
        auto& wall = input["entities"][0]["components"]["forge.physics_body"];
        auto& moving = input["entities"][1]["components"]["forge.physics_body"];
        if (policy == 0)
            moving["mask"] = 0;
        else if (policy == 1)
            wall["mask"] = 0;
        else if (policy == 2)
            moving["sensor"] = true;
        else
            wall["sensor"] = true;
        f.load(input);
        f.tick();
        check(y(f, "fast") < .5, "CCD bypassed masks or stopped a sensor interaction");
        check(f.scene.entity("fast").get<PhysicsBody>().continuous_collision,
              "Inactive CCD policy erased authored choice");
    }
    {
        Fixture f;
        auto input = source();
        auto& settings = input["entities"][1]["components"]["forge.physics_body"];
        settings["sensor"] = true;
        settings["continuous_collision"] = true;
        f.load(input);
        bool contact = false;
        for (unsigned tick = 0; tick < 90; ++tick) {
            f.tick();
            contact |= !f.physics->contacts().empty();
        }
        check(contact && y(f) < -1, "Stored CCD choice changed discrete sensor contacts");
    }
    for (unsigned motion : {0u, 1u}) {
        Fixture f;
        f.physics->configure(fast_config);
        auto input = fast_source();
        input["entities"][1]["components"]["forge.physics_body"]["motion"] = motion;
        f.load(input);
        if (motion == 1) {
            const auto ref = *f.engine.world().reference(f.scene.entity("fast").id());
            f.physics->move_kinematic(ref, {0, 0, 0}, {});
        }
        f.tick();
        check(std::abs(y(f, "fast") - (motion == 0 ? 3 : 0)) < 1e-6,
              "CCD incorrectly stopped or accelerated an inactive motion kind");
        check(f.scene.entity("fast").get<PhysicsBody>().continuous_collision,
              "Motion kind adoption erased stored CCD choice");
    }
    for (unsigned shape = 0; shape < 4; ++shape) {
        Fixture f;
        f.physics->configure(fast_config);
        auto input = fast_source();
        auto& c = input["entities"][1]["components"];
        LocalScale scale{-1, -1, -1};
        if (shape != 1) {
            c.erase("forge.sphere_collider");
            if (shape == 0) {
                c["forge.box_collider"] = {{"x", .1}, {"y", .1}, {"z", .1}};
                scale = {-.5, 2, .75};
            } else if (shape == 2)
                c["forge.capsule_collider"] = {{"radius", .05}, {"height", .1}};
            else {
                c["forge.cylinder_collider"] = {{"radius", .05}, {"height", .1}};
                scale = {-1, 2, -1};
            }
        }
        f.load(input);
        f.scene.entity("fast").set<LocalScale>(scale);
        f.physics->synchronize(0);
        f.tick();
        check(y(f, "fast") > 1.5 && y(f, "fast") < 1.9,
              "Admitted mirrored primitive tunneled with CCD");
        check(f.scene.entity("fast").get<LocalScale>() == scale,
              "CCD changed authored primitive scale signs");
    }
    {
        Fixture original, changed;
        original.load(source());
        changed.load(source());
        original.tick(10);
        changed.tick(10);
        auto entity = changed.scene.entity("cube");
        auto settings = entity.get<PhysicsBody>();
        settings.continuous_collision = true;
        entity.set<PhysicsBody>(settings);
        const auto before = y(changed);
        changed.physics->synchronize(0);
        check(y(changed) == before, "CCD body replacement moved its pose");
        for (unsigned tick = 0; tick < 15; ++tick) {
            original.tick();
            changed.tick();
            check(std::abs(y(original) - y(changed)) < 1e-6,
                  "CCD body replacement lost velocity before contact");
        }
        original.tick(360);
        changed.tick(360);
        check(std::abs(y(original) - y(changed)) < .02,
              "Slow continuous and discrete bodies did not settle on the same support");
        check(changed.physics->status().at("active_dynamic") == 0,
              "CCD comparison did not reach the sleeping precondition");
        settings.continuous_collision = false;
        entity.set<PhysicsBody>(settings);
        changed.physics->synchronize(0);
        check(changed.physics->status().at("active_dynamic") == 0,
              "Changing CCD woke a sleeping body");
        const auto snapshot = changed.scene.snapshot();
        auto malformed = snapshot;
        malformed["entities"][1]["components"]["forge.physics_body"]["continuous_collision"] =
            "true";
        reject([&] { changed.scene.restore_snapshot(malformed); });
        check(changed.scene.snapshot() == snapshot,
              "Invalid CCD value partially changed the admitted Scene");
    }
    {
        EngineContext authoring;
        Scene authored{authoring.world()};
        authored.restore_snapshot(source());
        const auto cube = authored.canonical_id("cube");
        auto settings = authored.entity(cube).get<PhysicsBody>();
        settings.continuous_collision = true;
        authored.entity(cube).set<PhysicsBody>(settings);
        const auto prefab = create_prefab_source(authored, cube);
        authored.set_prefab_sources({{prefab.asset(), prefab.source}});
        const auto one = instantiate_prefab(authored, prefab.asset());
        const auto two = instantiate_prefab(authored, prefab.asset());
        check(!authored.entity(one).owns<PhysicsBody>() &&
                  authored.entity(one).get<PhysicsBody>().continuous_collision,
              "Prefab did not inherit continuous collision");
        authoring_command(authored, "property.set",
                          {{"entity", one},
                           {"component", "forge.physics_body"},
                           {"field", "continuous_collision"},
                           {"value", false}});
        check(authored.entity(one).owns<PhysicsBody>() &&
                  !authored.entity(one).get<PhysicsBody>().continuous_collision &&
                  authored.entity(two).get<PhysicsBody>().continuous_collision,
              "CCD prefab override changed another instance");
        check(authored.undo() && authored.entity(one).get<PhysicsBody>().continuous_collision,
              "CCD prefab override Undo lost inherited choice");
        check(authored.redo() && !authored.entity(one).get<PhysicsBody>().continuous_collision,
              "CCD prefab override Redo lost explicit choice");
        EngineContext reopened;
        Scene saved{reopened.world()};
        saved.set_prefab_sources({{prefab.asset(), prefab.source}});
        saved.restore_snapshot(authored.snapshot());
        check(saved.snapshot() == authored.snapshot() &&
                  !saved.entity(one).get<PhysicsBody>().continuous_collision &&
                  saved.entity(two).get<PhysicsBody>().continuous_collision,
              "CCD prefab snapshot lost inheritance or overrides");
    }
}
