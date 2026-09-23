#include <forge/authoring.hpp>
#include <forge/prefab_authoring.hpp>
#include <forge/runtime.hpp>
#include <iostream>
using namespace forge;
void check(bool b, const char* text) {
    if (!b)
        throw std::runtime_error(text);
}
template <class F> void reject(F f) {
    try {
        f();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Expected rejection");
}
struct Fixture {
    Module module;
    EngineContext engine{WorldRole::Runtime, false, {physics_module()}};
    Scene scene{engine.world()};
    RuntimeSimulation simulation{engine.world(), scene, module};
    std::shared_ptr<PhysicsRuntime> physics =
        std::static_pointer_cast<PhysicsRuntime>(engine.services().physics());
    void tick(unsigned n = 1) {
        for (unsigned i = 0; i < n; ++i)
            simulation.tick(1.f / 60);
    }
    void load(const Json& source) {
        scene.restore_snapshot(source);
        physics->synchronize(0);
        simulation.reset_presentation();
    }
};
Json body(const char* id, double y, unsigned motion) {
    return {{"id", id},
            {"name", id},
            {"components",
             {{"forge.position", {{"x", 0}, {"y", y}, {"z", 0}}},
              {"forge.physics_body",
               {{"motion", motion},
                {"density", 1000},
                {"mass", 0},
                {"friction", .5},
                {"restitution", 0},
                {"gravity_factor", 1}}},
              {"forge.box_collider", {{"x", 1}, {"y", 1}, {"z", 1}}}}}};
}
Json source() {
    auto floor = body("floor", -.5, 0);
    floor["components"]["forge.box_collider"] = {{"x", 20}, {"y", 1}, {"z", 20}};
    return {{"version", 1}, {"entities", Json::array({floor, body("cube", 5, 2)})}};
}
double y(Fixture& f, const char* id = "cube") {
    return f.scene.entity(id).get<LocalTranslation>().y;
}
void collision_filters() {
    Fixture f;
    auto config = PhysicsConfig{};
    config.layers[1] = "Environment";
    config.layers[2] = "Actors";
    f.physics->configure(config);
    auto input = source();
    input["entities"][0]["components"]["forge.physics_body"]["layer"] = 1;
    input["entities"][1]["components"]["forge.physics_body"]["layer"] = 2;
    input["entities"][1]["components"]["forge.physics_body"]["mask"] = 0;
    f.load(input);
    const auto floor = f.physics->raycast_filtered({0, 10, 0}, {0, -20, 0}, {1u << 1, true});
    check(floor && floor->position[1] < .01, "Query layer did not select floor");
    PhysicsSweep sweep;
    sweep.origin = {0, 10, 0};
    sweep.displacement = {0, -20, 0};
    const auto volume = f.physics->shape_cast(sweep, {1u << 1, false});
    check(volume && volume->entity == floor->entity && std::abs(volume->fraction - .475) < .001 &&
              volume->normal[1] > .99,
          "Sphere sweep failed to report floor contact/normal/fraction");
    check(!f.physics->shape_cast(sweep, {0, false}), "Empty sweep mask hit");
    sweep.dimensions[0] = 0;
    reject([&] { f.physics->shape_cast(sweep, {}); });
    check(!f.physics->raycast_filtered({0, 10, 0}, {0, -20, 0}, {0, true}), "Empty query mask hit");
    f.tick(90);
    check(y(f) < -1, "Excluded body mask still collided with floor");
    Fixture sensor;
    sensor.load(source());
    auto cube = sensor.scene.entity("cube");
    auto body = cube.get<PhysicsBody>();
    body.sensor = true;
    cube.set<PhysicsBody>(body);
    sensor.physics->synchronize(0);
    auto solid = sensor.physics->raycast_filtered({0, 10, 0}, {0, -20, 0}, {UINT32_MAX, false});
    check(solid && solid->position[1] < .01, "Sensor exclusion failed");
    bool overlap = false;
    for (unsigned i = 0; i < 90; ++i) {
        sensor.tick();
        overlap |= !sensor.physics->contacts().empty();
    }
    check(overlap && y(sensor) < -1, "Sensor did not report overlap without solid response");
    body.enabled = false;
    cube.set<PhysicsBody>(body);
    sensor.physics->synchronize(0);
    check(sensor.physics->status().at("bodies") == 1, "Disabled body remained realized");
    body.enabled = true;
    body.layer = 31;
    cube.set<PhysicsBody>(body);
    reject([&] { sensor.physics->synchronize(0); });
    check(sensor.physics->status().at("bodies") == 1, "Invalid project layer damaged world");
}
void signed_scale_physics() {
    for (unsigned shape = 0; shape < 4; ++shape) {
        Fixture f;
        auto input = source();
        auto& components = input["entities"][1]["components"];
        if (shape != 0) {
            components.erase("forge.box_collider");
            if (shape == 1)
                components["forge.sphere_collider"] = {{"radius", .5}};
            else if (shape == 2)
                components["forge.capsule_collider"] = {{"radius", .5}, {"height", 1}};
            else
                components["forge.cylinder_collider"] = {{"radius", .5}, {"height", 1}};
        }
        f.load(input);
        auto entity = f.scene.entity("cube");
        for (float x : {-1.f, 1.f})
            for (float y : {-1.f, 1.f})
                for (float z : {-1.f, 1.f}) {
                    const LocalScale scale{x, y, z};
                    entity.set<LocalScale>(scale);
                    f.physics->synchronize(0);
                    auto hit = f.physics->raycast({0, 5, -10}, {0, 0, 20});
                    check(hit.has_value(), "Signed collider ray miss");
                    f.tick();
                    check(entity.get<LocalScale>() == scale,
                          "Physics changed authored scale signs");
                    check(equivalent(entity.get<LocalRotation>(), LocalRotation{}),
                          "Physics reflected rotation jumped");
                }
        entity.set<LocalScale>({-1, 1, 1});
        f.physics->synchronize(0);
        const auto snapshot = f.scene.snapshot(), checkpoint = f.physics->checkpoint();
        Fixture recovered;
        recovered.scene.restore_snapshot(snapshot);
        recovered.physics->restore(checkpoint);
        check(recovered.scene.entity("cube").get<LocalScale>() == LocalScale{-1, 1, 1},
              "Signed checkpoint scale lost");
        const auto count = f.physics->status().at("bodies");
        for (auto bad : {LocalScale{0, 1, 1}, LocalScale{1, 0, 1}, LocalScale{1, 1, 0},
                         LocalScale{0, 0, 0}, LocalScale{-1e-7f, 1, 1}}) {
            entity.set<LocalScale>(bad);
            check(entity.get<LocalScale>() == bad,
                  "Visual scale was rejected by physics authority");
            reject([&] { f.physics->synchronize(0); });
            check(f.physics->status().at("bodies") == count,
                  "Failed scale destroyed last-good bodies");
        }
        entity.set<LocalScale>({-2, 1, 1});
        if (shape == 0)
            f.physics->synchronize(0);
        else
            reject([&] { f.physics->synchronize(0); });
        if (shape == 3) {
            entity.set<LocalScale>({-1, 2, 1});
            f.physics->synchronize(0);
            check(f.physics->raycast({0, y(f), -10}, {0, 0, 20}).has_value(),
                  "Cylinder rejected independent Y scaling");
        }
        entity.set<LocalScale>({-1, 1, 1});
        f.physics->synchronize(0);
        f.tick();
    }
}
#include "character_physics.hpp"
#include "physics_hierarchy.hpp"
int main() {
    try {
        character_physics_tests();
        character_mechanics_tests();
        character_contract_tests();
        physics_hierarchy_tests();
        collision_filters();
        signed_scale_physics();
        {
            EngineContext authoring;
            check(!authoring.services().available(Capability::Physics), "Authoring created solver");
            check(authoring.world().world().lookup("forge.physics_body"), "Missing schema");
        }
        Fixture original;
        original.load(source());
        auto document = original.scene.snapshot();
        check(original.physics->status().at("bodies") == 2, "Body realization");
        original.tick(20);
        check(y(original) < 5 && y(original) > 4, "Gravity/fixed dt");
        auto snapshot = original.scene.snapshot();
        auto checkpoint = original.physics->checkpoint();
        Fixture recovered;
        recovered.scene.restore_snapshot(snapshot);
        recovered.physics->restore(checkpoint);
        recovered.simulation.reset_presentation();
        check(std::abs(y(original) - y(recovered)) < 1e-8, "Recovery pose");
        for (int i = 0; i < 100; ++i) {
            original.tick();
            recovered.tick();
            check(std::abs(y(original) - y(recovered)) < 1e-6,
                  "Recovery lost solver velocity/contact state");
        }
        check(std::abs(y(original) - .5) < .03, "Box rests on floor");
        original.tick(240);
        check(original.physics->status().at("sleeping_dynamic") == 1, "Body never slept");
        Fixture sleep;
        sleep.scene.restore_snapshot(original.scene.snapshot());
        sleep.physics->restore(original.physics->checkpoint());
        check(sleep.physics->status().at("sleeping_dynamic") == 1,
              "Restoration woke sleeping body");
        sleep.tick(20);
        check(std::abs(y(sleep) - y(original)) < 1e-6, "Sleep recovery");
        auto hit = original.physics->raycast({0, 10, 0}, {0, -20, 0});
        check(hit &&
                  hit->entity == *original.engine.world().reference(original.scene.entity("cube")),
              "Ray identity");
        auto ref = hit->entity;
        original.physics->teleport(ref, {2, 8, 0}, {}, true);
        original.tick();
        check(y(original) > 7.9, "Teleport");
        auto shown = original.simulation.presentation(0);
        for (const auto& e : shown.at("entities"))
            if (e.at("id") == original.scene.canonical_id("cube"))
                check(std::abs(e.at("components").at("forge.position").at("y").get<double>() -
                               e.at("world_affine").at(7).get<double>()) < 1e-6,
                      "Teleport interpolation reset");
        for (const char* field :
             {"version", "build", "jolt", "integrity", "gravity", "bytes", "mapping", "solver"}) {
            auto broken = checkpoint;
            broken[field] = nullptr;
            Fixture f;
            f.scene.restore_snapshot(snapshot);
            reject([&] { f.physics->restore(broken); });
        }
        {
            Fixture f;
            auto wrong = document;
            wrong["asset_id"] = AssetId::generate();
            f.scene.restore_snapshot(wrong);
            reject([&] { f.physics->restore(checkpoint); });
        } // pose-only mismatch is checked below separately
        {
            Fixture f;
            auto invalid = source();
            invalid["version"] = 3;
            invalid["asset_id"] = AssetId::generate();
            for (auto& e : invalid["entities"]) {
                e["id"] = EntityId::generate();
                e["spatial"] = {{"mode", "follow_structure"}};
                e["components"]["forge.local_translation"] = e["components"]["forge.position"];
                e["components"].erase("forge.position");
            }
            reject([&] { f.load(invalid); });
        }
        // Removal/recreation yields explicit sequence-bearing solver mapping, never creation-order
        // assumptions.
        original.scene.entity("cube").remove<PhysicsBody>();
        original.tick();
        check(original.physics->status().at("bodies") == 1, "Body removal");
        original.scene.entity("cube").set<PhysicsBody>({2});
        original.tick();
        auto recreated = original.physics->checkpoint();
        Fixture again;
        again.scene.restore_snapshot(original.scene.snapshot());
        again.physics->restore(recreated);
        again.tick();
        original.tick();
        check(std::abs(y(again) - y(original)) < 1e-6, "Recreated BodyID mapping");
        for (int shape = 0; shape < 2; ++shape) {
            auto doc = source();
            auto& c = doc["entities"][1]["components"];
            c.erase("forge.box_collider");
            if (shape == 0)
                c["forge.sphere_collider"] = {{"radius", .5}};
            else
                c["forge.capsule_collider"] = {{"radius", .5}, {"height", 1}};
            Fixture f;
            f.load(doc);
            f.tick(180);
            check(std::abs(y(f) - (shape ? .99 : .49)) < .06, "Primitive collider resting pose");
        }
        {
            Fixture f;
            auto doc = source();
            doc["entities"][1]["components"]["forge.sphere_collider"] = {{"radius", .5}};
            reject([&] { f.load(doc); });
        }
        {
            Fixture f;
            auto doc = source();
            auto& c = doc["entities"][1]["components"];
            c.erase("forge.box_collider");
            c["forge.sphere_collider"] = {{"radius", .5}};
            c["forge.scale"] = {{"x", 2}, {"y", 1}, {"z", 1}};
            reject([&] { f.load(doc); });
        }
        {
            EngineContext e;
            Scene s(e.world());
            s.reset(source());
            auto id = s.canonical_id("cube");
            authoring_command(s, "property.set",
                              {{"entity", id},
                               {"component", "forge.physics_body"},
                               {"field", "friction"},
                               {"value", .7}});
            check(s.entity(id).get<PhysicsBody>().friction > .69f, "Reflected edit");
            check(s.undo(), "Physics undo");
            check(s.entity(id).get<PhysicsBody>().friction == .5f, "Physics undo value");
        }
        {
            Fixture f;
            auto doc = source();
            doc["entities"][1]["components"]["forge.physics_body"]["motion"] = 1;
            f.load(doc);
            auto ref = *f.engine.world().reference(f.scene.entity("cube"));
            f.physics->move_kinematic(ref, {1, 6, 0}, {});
            f.tick();
            check(std::abs(y(f) - 6) < 1e-5, "Kinematic target");
            f.tick(5);
            check(std::abs(y(f) - 6) < 1e-5, "Kinematic moved without a new target");
        }
        {
            Fixture f;
            auto doc = source();
            doc["entities"][1]["components"]["forge.physics_body"]["motion"] = 0;
            f.load(doc);
            f.tick(20);
            check(y(f) == 5, "Static affected by gravity");
        }
        {
            Fixture f;
            auto doc = source();
            doc["entities"][1]["components"]["forge.rotation"] = {{"x", 25}, {"y", 0}, {"z", 15}};
            f.load(doc);
            bool contact = false;
            for (unsigned i = 0; i < 70; ++i) {
                f.tick();
                contact |= !f.physics->contacts().empty();
            }
            check(contact, "Missing contact notifications");
            auto q = f.scene.entity("cube").get<LocalRotation>();
            auto saved = f.physics->checkpoint();
            Fixture r;
            r.scene.restore_snapshot(f.scene.snapshot());
            r.physics->restore(saved);
            r.simulation.reset_presentation();
            for (int i = 0; i < 15; ++i) {
                r.tick();
                f.tick();
                check(equivalent(r.scene.entity("cube").get<LocalRotation>(),
                                 f.scene.entity("cube").get<LocalRotation>()),
                      "Angular state recovery");
            }
            check(!equivalent(q, f.scene.entity("cube").get<LocalRotation>()),
                  "Spinning recovery fixture had no angular motion");
        }
        {
            Fixture f;
            f.load(source());
            f.tick(10);
            const double before = y(f);
            auto e = f.scene.entity("cube");
            auto settings = e.get<PhysicsBody>();
            settings.mass = 5;
            settings.friction = .9f;
            e.set(settings);
            f.tick();
            check(y(f) < before - .02, "Compatible rebuild lost downward velocity");
            check(f.physics->status().at("bodies") == 2, "Rebuild leaked body");
        }
        {
            Fixture f;
            auto doc = source();
            doc["entities"][1]["components"]["forge.physics_body"]["gravity_factor"] = 0;
            f.load(doc);
            f.tick(30);
            check(y(f) == 5, "Gravity factor");
        }
        check(original.physics->status().at("bodies") == 2, "World isolation");
        {
            EngineContext engine;
            Scene scene(engine.world());
            scene.reset(source());
            auto prefab = create_prefab_source(scene, scene.canonical_id("cube"));
            scene.set_prefab_sources({{prefab.asset(), prefab.source}});
            auto id = instantiate_prefab(scene, prefab.asset());
            authoring_command(
                scene, "transform.binding",
                {{"entity", id}, {"spatial", {{"mode", "world"}}}, {"mode", "keep_local"}});
            check(scene.entity(id).has<PhysicsBody>() && !scene.entity(id).owns<PhysicsBody>(),
                  "Physics prefab inheritance");
            authoring_command(scene, "property.set",
                              {{"entity", id},
                               {"component", "forge.physics_body"},
                               {"field", "friction"},
                               {"value", .5}});
            auto next = prefab.source;
            next["revision"] = 2;
            next["members"][0]["components"]["forge.physics_body"]["friction"] = .8;
            next["members"][0]["components"]["forge.physics_body"]["restitution"] = .3;
            scene.publish_prefab_sources({{prefab.asset(), next}}, [] {});
            auto b = scene.entity(id).get<PhysicsBody>();
            check(b.friction == .5f && b.restitution == .3f, "Independent/equal physics override");
            authoring_command(
                scene, "property.revert",
                {{"entity", id}, {"component", "forge.physics_body"}, {"field", "friction"}});
            check(scene.entity(id).get<PhysicsBody>().friction == .8f, "Physics property Revert");
            check(scene.undo() && scene.entity(id).get<PhysicsBody>().friction == .5f,
                  "Revert undo");
            auto copy = scene.duplicate_subtree(id);
            check(scene.reference(copy) != scene.reference(id),
                  "Physics instance duplicate identity");
            Fixture runtime;
            runtime.load(scene.snapshot());
            runtime.tick(2);
            check(runtime.physics->status().at("bodies") == 4, "Prefab runtime realization");
            auto bad = next;
            bad["members"][0]["components"]["forge.physics_body"]["friction"] = -1;
            auto before = scene.snapshot();
            reject([&] { scene.publish_prefab_sources({{prefab.asset(), bad}}, [] {}); });
            check(scene.snapshot() == before,
                  "Failed physics prefab publication changed instances");
        }
        {
            Fixture f;
            f.load(source());
            f.scene.entity("cube").remove<BoxCollider>();
            reject([&] { f.tick(); });
        }
        auto retained = original.physics;
        original.engine.world().modules().stop();
        reject([&] { retained->raycast({}, {0, -1, 0}); });
        std::cout << "Physics module, motion, colliders, recovery and authoring passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
