#pragma once
#include <limits>
#include <numbers>

void physics_motion_tuning_tests() {
    const float dt = 1.f / 60;
    for (float step : {1.f / 30, dt})
        for (float damping : {0.f, .5f, 2.f, 100.f}) {
            Fixture f;
            auto input = free_body(2);
            auto& body = input["entities"][0]["components"]["forge.physics_body"];
            body["linear_damping"] = damping;
            body["angular_damping"] = damping;
            body["allow_sleeping"] = false;
            f.physics->configure(PhysicsConfig{{0, 0, 0}});
            f.load(input);
            f.physics->add_impulse(cube_ref(f), {12, 0, 0});
            f.physics->add_angular_impulse(cube_ref(f), {0, 0, 1});
            double velocity = 6, angular = 3, position = 0, angle = 0;
            for (unsigned tick = 0; tick < 30; ++tick) {
                velocity *= std::max(0., 1. - double(damping) * step);
                angular *= std::max(0., 1. - double(damping) * step);
                position += velocity * step;
                angle += angular * step;
                f.simulation.tick(step);
                const auto q = f.scene.entity("cube").get<LocalRotation>();
                check(std::abs(coordinate(f, 0) - position) < 2e-5 &&
                          std::abs(q.z - std::sin(angle * .5)) < 2e-5 &&
                          std::abs(q.w - std::cos(angle * .5)) < 2e-5,
                      "Authored damping failed independent per-step linear/angular equation");
            }
        }
    for (float limit : {0.f, 3.f, 500.f}) {
        Fixture f;
        free_world(f, 1);
        auto entity = f.scene.entity("cube");
        auto value = entity.get<PhysicsBody>();
        value.linear_damping = 0;
        value.max_linear_velocity = limit;
        value.angular_damping = 0;
        value.max_angular_velocity = limit == 0 ? 0 : 90;
        entity.set<PhysicsBody>(value);
        f.physics->add_impulse(cube_ref(f), {1000, 1000, 0});
        f.physics->add_angular_impulse(cube_ref(f), {0, 0, 100});
        f.tick();
        const double distance = double(limit) / std::sqrt(2.) * dt;
        const double angle = (limit == 0 ? 0 : std::numbers::pi / 2) * dt;
        const auto p = entity.get<LocalTranslation>();
        const auto q = entity.get<LocalRotation>();
        check(std::abs(p.x - distance) < 2e-6 && std::abs(p.y - distance) < 2e-6 &&
                  std::abs(p.z) < 1e-6 && std::abs(q.z - std::sin(angle * .5)) < 1e-6 &&
                  std::abs(q.w - std::cos(angle * .5)) < 1e-6,
              "Speed limit failed vector-magnitude clamp, zero limit or degrees conversion");
    }
    {
        Fixture f;
        free_world(f, 1);
        f.physics->add_angular_impulse(cube_ref(f), {0, 0, 100});
        f.tick();
        const auto q = f.scene.entity("cube").get<LocalRotation>();
        const double angle = (2700. * std::numbers::pi / 180) * (1. - .05 * double(dt)) * dt;
        check(std::abs(q.z - std::sin(angle * .5)) < 1e-6,
              "Backfilled angular speed changed the existing native default");
    }
    {
        Fixture f;
        free_world(f, 1);
        auto entity = f.scene.entity("cube");
        auto settings = entity.get<PhysicsBody>();
        settings.linear_damping = settings.angular_damping = 0;
        entity.set<PhysicsBody>(settings);
        f.physics->add_impulse(cube_ref(f), {6, 0, 0});
        f.physics->add_angular_impulse(cube_ref(f), {0, 0, 1});
        f.tick();
        const auto p = entity.get<LocalTranslation>();
        const auto q = entity.get<LocalRotation>();
        settings.linear_damping = 2;
        settings.angular_damping = 3;
        settings.max_linear_velocity = 1;
        settings.max_angular_velocity = 90;
        entity.set<PhysicsBody>(settings);
        f.physics->synchronize(0);
        check(entity.get<LocalTranslation>() == p && entity.get<LocalRotation>() == q,
              "Motion tuning replacement changed pose before integration");
        f.tick();
        const double angular = (std::numbers::pi / 2) * (1. - 3 * double(dt));
        const double angle = 6 * double(dt) + angular * dt;
        check(std::abs(coordinate(f, 0) - p.x - (1. - 2 * double(dt)) * dt) < 1e-6 &&
                  std::abs(entity.get<LocalRotation>().z - std::sin(angle * .5)) < 1e-6,
              "Replacement lost velocity or failed to clamp to lowered limits");
    }
    {
        Fixture f;
        free_world(f, 1);
        f.tick(360);
        check(f.physics->status().at("sleeping_dynamic") == 1,
              "Free body did not reach the sleeping precondition");
        auto entity = f.scene.entity("cube");
        auto settings = entity.get<PhysicsBody>();
        settings.linear_damping = 1;
        entity.set<PhysicsBody>(settings);
        f.physics->synchronize(0);
        check(f.physics->status().at("sleeping_dynamic") == 1,
              "Changing damping woke a permitted sleeping body");
        settings.allow_sleeping = false;
        entity.set<PhysicsBody>(settings);
        f.physics->synchronize(0);
        f.tick(360);
        check(f.physics->status().at("active_dynamic") == 1 && coordinate(f, 0) == 0,
              "Disabling sleep left a body asleep or introduced artificial motion");
        settings.allow_sleeping = true;
        entity.set<PhysicsBody>(settings);
        f.physics->synchronize(0);
        f.tick(360);
        check(f.physics->status().at("sleeping_dynamic") == 1,
              "Re-enabling sleep did not let a stationary body settle");
    }
    {
        Fixture original;
        free_world(original, 2);
        auto entity = original.scene.entity("cube");
        auto settings = entity.get<PhysicsBody>();
        settings.linear_damping = .7f;
        settings.angular_damping = .8f;
        settings.max_linear_velocity = 2;
        settings.max_angular_velocity = 120;
        settings.allow_sleeping = false;
        entity.set<PhysicsBody>(settings);
        original.physics->add_impulse(cube_ref(original), {10, 0, 0});
        original.physics->add_angular_impulse(cube_ref(original), {0, 0, 2});
        original.tick(20);
        original.physics->add_force(cube_ref(original), {0, 1, 0});
        const auto checkpoint = original.physics->checkpoint();
        Fixture restored;
        restored.physics->configure(PhysicsConfig{{0, 0, 0}});
        restored.scene.restore_snapshot(original.scene.snapshot());
        restored.physics->restore(checkpoint);
        for (unsigned tick = 0; tick < 30; ++tick) {
            original.tick();
            restored.tick();
            check(equivalent(entity.get<LocalTranslation>(),
                             restored.scene.entity("cube").get<LocalTranslation>()) &&
                      equivalent(entity.get<LocalRotation>(),
                                 restored.scene.entity("cube").get<LocalRotation>()),
                  "Motion tuning or pending load lost numerical recovery continuation");
        }
        check(restored.scene.entity("cube").get<PhysicsBody>() == settings,
              "Recovery changed authored tuning settings");
    }
    for (unsigned field = 0; field < 4; ++field)
        for (float invalid : {-1.f, 1000001.f, std::numeric_limits<float>::infinity(),
                              std::numeric_limits<float>::quiet_NaN()}) {
            Fixture f;
            free_world(f, 1);
            auto entity = f.scene.entity("cube");
            const auto before = entity.get<LocalTranslation>();
            const auto valid = entity.get<PhysicsBody>();
            auto bad = valid;
            float* fields[] = {&bad.linear_damping, &bad.angular_damping, &bad.max_linear_velocity,
                               &bad.max_angular_velocity};
            *fields[field] = invalid;
            f.physics->add_impulse(cube_ref(f), {6, 0, 0});
            entity.set<PhysicsBody>(bad);
            reject([&] { f.physics->synchronize(dt); });
            check(entity.get<LocalTranslation>() == before && f.physics->status().at("bodies") == 1,
                  "Invalid typed tuning partially mutated native body or pose");
            entity.set<PhysicsBody>(valid);
            f.tick();
            check(std::abs(coordinate(f, 0) - 6 * (1. - .05 * double(dt)) * dt) < 1e-6,
                  "Rejected typed tuning discarded an earlier queued impulse");
        }
    {
        EngineContext engine;
        Scene scene{engine.world()};
        scene.restore_snapshot(source());
        auto defaults = scene.entity("cube").get<PhysicsBody>();
        check(defaults.linear_damping == .05f && defaults.angular_damping == .05f &&
                  defaults.max_linear_velocity == 500 && defaults.max_angular_velocity == 2700 &&
                  defaults.allow_sleeping,
              "Old authored body failed additive default admission");
        const auto before = scene.snapshot();
        for (const auto* field :
             {"linear_damping", "angular_damping", "max_linear_velocity", "max_angular_velocity"})
            for (const Json& invalid : {Json(-1), Json(1000001), Json("1"), Json(nullptr)}) {
                auto candidate = before;
                candidate["entities"][1]["components"]["forge.physics_body"][field] = invalid;
                reject([&] { scene.restore_snapshot(candidate); });
                check(scene.snapshot() == before, "Invalid tuning partially mutated authoring");
            }
        auto candidate = before;
        candidate["entities"][1]["components"]["forge.physics_body"]["allow_sleeping"] = 0;
        reject([&] { scene.restore_snapshot(candidate); });
        auto settings = defaults;
        settings.linear_damping = 1;
        settings.angular_damping = 2;
        settings.max_linear_velocity = 3;
        settings.max_angular_velocity = 180;
        settings.allow_sleeping = false;
        scene.entity("cube").set<PhysicsBody>(settings);
        auto prefab = create_prefab_source(scene, scene.canonical_id("cube"));
        scene.set_prefab_sources({{prefab.asset(), prefab.source}});
        const auto one = instantiate_prefab(scene, prefab.asset());
        const auto two = instantiate_prefab(scene, prefab.asset());
        const Json overrides{{"linear_damping", .3},
                             {"angular_damping", .4},
                             {"max_linear_velocity", 5},
                             {"max_angular_velocity", 90},
                             {"allow_sleeping", true}};
        for (const auto& [field, value] : overrides.items()) {
            const auto previous = scene.entity(one).get<PhysicsBody>();
            authoring_command(scene, "property.set",
                              {{"entity", one},
                               {"component", "forge.physics_body"},
                               {"field", field},
                               {"value", value}});
            const auto changed = scene.entity(one).get<PhysicsBody>();
            check(changed != previous && scene.entity(two).get<PhysicsBody>() == settings,
                  "Tuning prefab override changed another instance");
            check(scene.undo() && scene.entity(one).get<PhysicsBody>() == previous,
                  "Tuning override Undo failed");
            check(scene.redo() && scene.entity(one).get<PhysicsBody>() == changed,
                  "Tuning override Redo failed");
        }
        EngineContext reopened;
        Scene saved{reopened.world()};
        saved.set_prefab_sources({{prefab.asset(), prefab.source}});
        saved.restore_snapshot(scene.snapshot());
        check(saved.entity(one).get<PhysicsBody>() == scene.entity(one).get<PhysicsBody>(),
              "Save/reopen lost authored tuning override settings");
        authoring_command(
            scene, "property.revert",
            {{"entity", one}, {"component", "forge.physics_body"}, {"field", "linear_damping"}});
        check(scene.entity(one).get<PhysicsBody>().linear_damping == 1 &&
                  scene.entity(one).get<PhysicsBody>().angular_damping == .4f,
              "Tuning revert discarded independent field overrides");
    }
}
