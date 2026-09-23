#pragma once
#include "../src/physics_debug.hpp"
#include <forge/character_components.hpp>
#include <numbers>
void character_physics_tests() {
    Fixture f;
    auto input = source();
    auto& character = input["entities"][1];
    character["id"] = "character";
    character["components"].erase("forge.physics_body");
    character["components"].erase("forge.box_collider");
    Json defaults = Json::object();
    const auto schema = f.scene.schema();
    for (const auto& component : schema.at("components"))
        if (component.at("id") == "forge.character_controller")
            for (const auto& field : component.at("fields"))
                defaults[field.at("id").get<std::string>()] = field.at("default");
    check(!defaults.empty(), "Character schema missing");
    character["components"]["forge.character_controller"] = defaults;
    auto ceiling = body("ceiling", 1.4, 0);
    ceiling["components"]["forge.position"]["x"] = 3;
    ceiling["components"]["forge.box_collider"] = {{"x", 2}, {"y", .2}, {"z", 2}};
    input["entities"].push_back(ceiling);
    f.scene.restore_snapshot(input);
    auto entity = f.scene.entity("character");
    entity.set<SpatialBinding>({SpatialMode::World, {}});
    f.physics->synchronize(0);
    f.simulation.reset_presentation();
    const auto ref = *f.engine.world().reference(entity.id());
    f.tick(180);
    auto state = f.physics->character(ref);
    check(state.ground == CharacterGround::OnGround && std::abs(state.position.y) < .1,
          "Character failed to settle on floor");
    check(state.supporting_entity.has_value(), "Character has no copied ground entity");
    const auto hit = f.physics->raycast({0, 1, -5}, {0, 0, 10});
    check(hit && hit->entity == ref, "Character inner body not mapped to EntityRef");
    f.physics->move_character(ref, {1, 0, 0});
    f.tick(60);
    state = f.physics->character(ref);
    check(std::abs(state.position.x - 1) < .1, "Character movement intent did not persist");
    f.physics->move_character(ref, {0, 0, 0});
    f.physics->jump_character(ref, 4);
    f.tick();
    state = f.physics->character(ref);
    check(state.jump_accepted && state.position.y > .01, "Grounded character jump failed");
    {
        Fixture airborne;
        airborne.scene.restore_snapshot(f.scene.snapshot());
        airborne.physics->restore(f.physics->checkpoint());
        check(airborne.physics->character(ref).jump_accepted,
              "Recovery lost the just-accepted jump result");
        f.tick();
        airborne.tick();
        check(equivalent(f.physics->character(ref).position,
                         airborne.physics->character(ref).position),
              "Mid-jump recovery changed the next simulated pose");
    }
    f.tick(120);
    check(f.physics->character(ref).ground == CharacterGround::OnGround, "Jump failed to land");
    f.physics->crouch_character(ref, true);
    f.tick();
    check(f.physics->character(ref).crouched, "Character did not crouch");
    f.physics->place_character(ref, {3, 0, 0}, {}, true);
    f.tick();
    check(std::abs(f.physics->character(ref).position.x - 3) < .01, "Crouched placement failed");
    f.physics->crouch_character(ref, false);
    f.tick();
    state = f.physics->character(ref);
    check(state.crouched && state.shape_change_blocked, "Character stood through ceiling");
    f.physics->place_character(ref, {3, -1, 0}, {}, true);
    f.physics->synchronize(0);
    check(f.physics->character(ref).shape_change_blocked &&
              f.physics->character(ref).position.y > -.1,
          "Blocked character placement changed position");
    const auto snapshot = f.scene.snapshot(), checkpoint = f.physics->checkpoint();
    const auto row =
        std::find_if(snapshot.at("entities").begin(), snapshot.at("entities").end(),
                     [&](const auto& item) { return item.at("id") == ref.entity.str(); });
    const auto preview = prepare_physics_debug(row->at("components"), {1, 1, 1}, {}, true);
    check(!preview.triangles.empty() && !preview.truncated,
          "Native crouched character preview missing");
    float top = 0;
    for (const auto& tri : preview.triangles)
        for (const auto& p : tri)
            top = std::max(top, p[1]);
    check(std::abs(top - 1.15f) < .01f,
          "Native character preview does not match crouched dimensions");
    Fixture recovered;
    recovered.scene.restore_snapshot(snapshot);
    recovered.physics->restore(checkpoint);
    auto before = recovered.physics->character(ref);
    check(before.crouched && before.ground == state.ground,
          "Character recovery lost shape/ground state");
    f.tick();
    recovered.tick();
    check(
        equivalent(f.physics->character(ref).position, recovered.physics->character(ref).position),
        "Character diverged immediately after recovery");
    const auto scale = entity.has<LocalScale>() ? entity.get<LocalScale>() : LocalScale{};
    entity.set<LocalScale>({0, 1, 1});
    reject([&] { f.physics->synchronize(0); });
    check(f.physics->status().at("characters") == 1,
          "Invalid character scale destroyed old controller");
    entity.set<LocalScale>(scale);
    f.physics->synchronize(0);
}

struct CharacterFixture : Fixture {
    EntityRef ref;
    void load_character(Json input, LocalTranslation position = {0, .02, 0},
                        CharacterController config = {}) {
        scene.restore_snapshot(input);
        auto entity = scene.entity("cube");
        entity.remove<PhysicsBody>();
        entity.remove<BoxCollider>();
        entity.set<CharacterController>(config);
        entity.set<SpatialBinding>({SpatialMode::World, {}});
        entity.set<LocalTranslation>(position);
        physics->synchronize(0);
        simulation.reset_presentation();
        ref = *engine.world().reference(entity.id());
    }
    CharacterState state() const { return physics->character(ref); }
    EntityRef reference(const char* id) { return *engine.world().reference(scene.entity(id).id()); }
};
void character_mechanics_tests() {
    {
        CharacterFixture f;
        auto config = CharacterController{};
        config.gravity_factor = 0;
        f.load_character(source(), {0, 10, 0}, config);
        f.physics->move_character(f.ref, {1, 0, 0});
        f.tick();
        f.physics->move_character(f.ref, {});
        f.physics->place_character(f.ref, {0, 10, 0}, rotation_from_euler({0, 0, 90}), false);
        f.tick();
        check(std::abs(f.state().velocity[0] - 1) < .001 && std::abs(f.state().velocity[1]) < .001,
              "Placement with a new up axis did not preserve world velocity");
    }
    for (float height : {.25f, .8f}) {
        CharacterFixture f;
        auto input = source();
        auto obstacle = body("step", height * .5, 0);
        obstacle["components"]["forge.position"]["z"] = 2;
        obstacle["components"]["forge.box_collider"] = {{"x", 4}, {"y", height}, {"z", 2}};
        input["entities"].push_back(obstacle);
        f.load_character(input);
        f.tick(10);
        f.physics->move_character(f.ref, {0, 0, 1});
        double maximum_y = 0;
        for (unsigned i = 0; i < 150; ++i) {
            f.tick();
            maximum_y = std::max(maximum_y, f.state().position.y);
        }
        if (height < .4f)
            check(f.state().position.z > 2 && maximum_y > .2, "Permitted step not climbed");
        else
            check(f.state().position.z < .7 && maximum_y < .1, "Character climbed too-tall step");
    }
    {
        CharacterFixture f;
        auto input = source();
        for (unsigned i = 0; i < 5; ++i) {
            auto step = body(("stair" + std::to_string(i)).c_str(), .125 * (i + 1), 0);
            step["components"]["forge.position"]["z"] = 1.5 + i * .6;
            step["components"]["forge.box_collider"] = {{"x", 3}, {"y", .25 * (i + 1)}, {"z", .6}};
            input["entities"].push_back(step);
        }
        f.load_character(input);
        f.tick(10);
        f.physics->move_character(f.ref, {0, 0, 1});
        double maximum = 0;
        for (unsigned i = 0; i < 240; ++i) {
            f.tick();
            maximum = std::max(maximum, f.state().position.y);
        }
        check(maximum > 1.2, "Stair series failed");
    }
    for (float degrees : {15.f, 44.f, 44.99f, 45.f, 45.01f, 46.f, 65.f}) {
        CharacterFixture f;
        auto input = source();
        // Thin rotated box top is a finite plane through (0,0,0).

        input["entities"][0]["components"]["forge.rotation"] = {{"x", degrees}, {"y", 0}, {"z", 0}};
        input["entities"][0]["components"]["forge.position"]["y"] =
            -.5 * std::cos(degrees * std::numbers::pi / 180);
        input["entities"][0]["components"]["forge.position"]["z"] =
            -.5 * std::sin(degrees * std::numbers::pi / 180);
        CharacterController config;
        config.max_slope = 45;
        config.step_height = 0;
        f.load_character(input, {0, 1, 0}, config);
        f.tick(45);
        const auto ground = f.state().ground;
        if (degrees < 45)
            check(ground == CharacterGround::OnGround, "Walkable slope reported unsupported");
        else if (degrees > 45)
            check(ground != CharacterGround::OnGround, "Steep slope reported walkable");
        else
            check(std::isfinite(f.state().position.y) && std::isfinite(f.state().velocity[1]),
                  "Exact slope threshold produced invalid state");
        if (degrees < 45) {
            const auto p = f.state().position;
            f.physics->move_character(f.ref, {0, 0, 1});
            f.tick(30);
            check(f.state().position.z > p.z + .3, "Movement up walkable slope failed");
            f.physics->move_character(f.ref, {0, 0, -1});
            f.tick(30);
            check(f.state().ground == CharacterGround::OnGround,
                  "Movement down slope lost support");
        }
    }
    {
        CharacterFixture f;
        auto input = source();
        input["entities"][0]["components"]["forge.physics_body"]["motion"] = 1;
        f.load_character(input, {2, .02, 0});
        f.tick(10);
        const auto platform = f.reference("floor");
        const auto start = f.state().position;
        for (unsigned i = 1; i <= 60; ++i) {
            f.physics->move_kinematic(platform, {i / 60., -.5, 0}, {});
            f.tick();
        }
        check(std::abs(f.state().position.x - start.x - 1) < .06,
              "Translating platform failed to carry character");
        check(f.state().supporting_entity == platform &&
                  std::abs(f.state().ground_velocity[0] - 1) < .02,
              "Platform identity/velocity missing");
        f.physics->jump_character(f.ref, 4);
        f.physics->move_kinematic(platform, {61 / 60., -.5, 0}, {});
        f.tick();
        check(f.state().jump_accepted && f.state().velocity[0] > .9,
              "Jump lost platform takeoff velocity");
        f.tick(5);
        check(f.state().velocity[0] > .9, "Airborne platform momentum lost");
    }
    {
        CharacterFixture f;
        auto input = source();
        input["entities"][0]["components"]["forge.physics_body"]["motion"] = 1;
        f.load_character(input, {2, .02, 0});
        f.tick(10);
        const auto platform = f.reference("floor");
        for (unsigned i = 1; i <= 60; ++i) {
            f.physics->move_kinematic(platform, {0, -.5, 0},
                                      rotation_from_euler({0, double(i), 0}));
            f.tick();
        }
        const auto p = f.state().position;
        check(f.state().ground == CharacterGround::OnGround &&
                  std::abs(std::hypot(p.x, p.z) - 2) < .1 && std::abs(p.z) > 1.5,
              "Rotating platform failed to carry character");
    }
    for (float mass : {1.f, 100000.f}) {
        CharacterFixture f;
        auto input = source();
        auto block = body("push", .5, 2);
        block["components"]["forge.position"]["z"] = 1.5;
        block["components"]["forge.physics_body"]["mass"] = mass;
        input["entities"].push_back(block);
        f.load_character(input);
        f.tick(10);
        f.physics->move_character(f.ref, {0, 0, 1});
        f.tick(120);
        const auto z = f.scene.entity("push").get<LocalTranslation>().z;
        if (mass < 10)
            check(z > 1.7, "Character failed to push light dynamic body");
        else
            check(z < 1.6 && f.state().position.z < .8, "Character pushed through heavy body");
    }
}

void character_contract_tests() {
    for (bool sensor : {false, true}) {
        CharacterFixture f;
        auto settings = PhysicsConfig{};
        settings.layers[1] = "Obstacles";
        f.physics->configure(settings);
        auto input = source();
        auto barrier = body("barrier", 1, 0);
        barrier["components"]["forge.position"]["z"] = 1;
        barrier["components"]["forge.box_collider"] = {{"x", 3}, {"y", 2}, {"z", .2}};
        barrier["components"]["forge.physics_body"]["layer"] = 1;
        barrier["components"]["forge.physics_body"]["sensor"] = sensor;
        input["entities"].push_back(barrier);
        auto config = CharacterController{};
        config.mask = sensor ? UINT32_MAX : 1; // Ignore obstacle layer, retain floor.
        f.load_character(input, {}, config);
        f.tick(10);
        f.physics->move_character(f.ref, {0, 0, 1});
        f.tick(120);
        check(f.state().position.z > 1.9,
              "Character was blocked by excluded layer or non-solid sensor");
        const auto hit = f.physics->raycast_filtered({0, 1, -2}, {0, 0, 5}, {2, true});
        check(hit && hit->entity == f.reference("barrier"),
              "Filtered query failed to report obstacle/sensor");
        if (sensor)
            check(!f.physics->raycast_filtered({0, 1, -2}, {0, 0, 5}, {2, false}),
                  "Sensor-excluding query reported a sensor");
    }
    {
        CharacterFixture f;
        f.load_character(source());
        // Author through the existing scene identity owner, not an untracked Flecs entity.
        const auto snapshot = f.scene.snapshot();
        auto input = snapshot;
        auto row =
            *std::find_if(input["entities"].begin(), input["entities"].end(),
                          [&](const Json& value) { return value.at("id") == f.ref.entity.str(); });
        row["id"] = EntityId::generate().str();
        row["name"] = "Second character";
        row["components"]["forge.local_translation"]["z"] = 2;
        input["entities"].push_back(row);
        f.scene.restore_snapshot(input);
        f.physics->synchronize(0);
        f.tick(10);
        f.physics->move_character(f.ref, {0, 0, 1});
        f.tick(180);
        check(f.physics->status().at("characters") == 2 && f.state().position.z < 1.4,
              "Character passed through a second controller's inner body");
    }
    {
        CharacterFixture f;
        f.load_character(source());
        f.tick(10);
        reject([&] { f.physics->move_character(f.ref, {0, 1, 0}); });
        f.tick();
        check(f.state().ground == CharacterGround::OnGround,
              "Rejected movement poisoned character queue");
        auto entity = f.scene.entity("cube");
        const auto before = f.scene.snapshot();
        f.simulation.presentation(0);
        check(f.scene.snapshot() == before, "Presentation advanced character simulation");
        auto config = entity.get<CharacterController>();
        config.enabled = false;
        entity.set(config);
        f.physics->synchronize(0);
        check(f.physics->status().at("characters") == 0, "Disabled controller still realized");
        reject([&] { f.physics->move_character(f.ref, {0, 0, 1}); });
        config.enabled = true;
        config.shape = 1;
        entity.set(config);
        entity.set<LocalScale>({-1, 2, 1});
        f.physics->synchronize(0);
        f.tick(10);
        check(f.state().ground == CharacterGround::OnGround,
              "Cylinder character or independent Y scale failed");
        entity.set<LocalScale>({2, 1, 1});
        reject([&] { f.physics->synchronize(0); });
        entity.set<LocalScale>({-1, 2, 1});
        f.physics->synchronize(0);
        auto child = f.scene.entity("floor");
        child.set<SpatialBinding>({SpatialMode::Explicit, f.ref});
        reject([&] { f.physics->synchronize(0); });
        child.set<SpatialBinding>({SpatialMode::World, {}});
        f.physics->synchronize(0);
        const auto snapshot = f.scene.snapshot();
        check(snapshot.dump().find("ground_velocity") == std::string::npos,
              "Transient controller contacts serialized as authored state");
        f.physics->stop();
    }
    {
        CharacterFixture f;
        auto input = source();
        auto wall = body("wall", 1.5, 0);
        wall["components"]["forge.position"]["z"] = 2;
        wall["components"]["forge.box_collider"] = {{"x", 4}, {"y", 3}, {"z", .5}};
        input["entities"].push_back(wall);
        wall["id"] = "side";
        wall["components"]["forge.position"]["x"] = 2;
        wall["components"]["forge.position"]["z"] = 0;
        wall["components"]["forge.box_collider"] = {{"x", .5}, {"y", 3}, {"z", 4}};
        input["entities"].push_back(wall);
        f.load_character(input);
        f.tick(10);
        f.physics->move_character(f.ref, {1, 0, 1});
        f.tick(180);
        check(f.state().position.x < 1.42 && f.state().position.z < 1.42 &&
                  f.state().ground == CharacterGround::OnGround,
              "Character crossed wall/corner");
    }
    {
        EngineContext engine;
        Scene scene(engine.world());
        scene.reset(source());
        auto entity = scene.entity("cube");
        entity.remove<PhysicsBody>();
        entity.remove<BoxCollider>();
        entity.set<CharacterController>({});
        entity.set<SpatialBinding>({SpatialMode::World, {}});
        auto prefab = create_prefab_source(scene, scene.canonical_id("cube"));
        scene.set_prefab_sources({{prefab.asset(), prefab.source}});
        const auto id = instantiate_prefab(scene, prefab.asset());
        check(!scene.entity(id).owns<CharacterController>(),
              "Character prefab default was materialized");
        authoring_command(scene, "property.set",
                          {{"entity", id},
                           {"component", "forge.character_controller"},
                           {"field", "max_slope"},
                           {"value", 50}});
        auto next = prefab.source;
        next["revision"] = 2;
        next["members"][0]["components"]["forge.character_controller"]["max_slope"] = 40;
        next["members"][0]["components"]["forge.character_controller"]["step_height"] = .3;
        scene.publish_prefab_sources({{prefab.asset(), next}}, [] {});
        check(scene.entity(id).get<CharacterController>().max_slope == 50 &&
                  scene.entity(id).get<CharacterController>().step_height == .3f,
              "Character equal override blocked independent prefab propagation");
        authoring_command(
            scene, "property.revert",
            {{"entity", id}, {"component", "forge.character_controller"}, {"field", "max_slope"}});
        check(scene.entity(id).get<CharacterController>().max_slope == 40,
              "Character property Revert failed");
        check(scene.undo() && scene.entity(id).get<CharacterController>().max_slope == 50,
              "Character Revert Undo failed");
        EngineContext copy;
        Scene restored(copy.world());
        restored.restore_snapshot(scene.snapshot());
        check(restored.entity(id).get<CharacterController>().max_slope == 50,
              "Character prefab round trip lost override");
    }
}
