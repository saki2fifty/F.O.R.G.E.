#include <cmath>
#include <forge/character_components.hpp>
#include <forge/runtime.hpp>
#include <iostream>

using namespace forge;
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F&& operation) {
    try {
        operation();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid 2D physics operation was accepted");
}
Json row(std::string id, const char* name, double x, double y, unsigned motion) {
    return {{"id", id},
            {"name", name},
            {"spatial", {{"mode", "world"}}},
            {"components",
             {{"forge.local_translation", {{"x", x}, {"y", y}, {"z", 7}}},
              {"forge.local_scale", {{"x", -1}, {"y", 1}, {"z", 0}}},
              {"forge.physics_body_2d", {{"motion", motion}}},
              {"forge.box_collider_2d", {{"x", 1}, {"y", 1}}}}}};
}
struct Fixture {
    EngineContext engine{WorldRole::Runtime, false, {physics_module()}};
    Scene scene{engine.world()};
    RuntimeSimulation simulation{engine.world(), scene};
    std::shared_ptr<PhysicsRuntime> physics =
        std::static_pointer_cast<PhysicsRuntime>(engine.services().physics());
    std::string floor = EntityId::generate().str(), actor = EntityId::generate().str();
    Json source() const {
        auto ground = row(floor, "Floor", 0, -.5, 0);
        ground["components"]["forge.box_collider_2d"] = {{"x", 20}, {"y", 1}};
        return {{"version", 5},
                {"asset_id", AssetId::generate()},
                {"entities", Json::array({ground, row(actor, "Actor", 0, 5, 2)})}};
    }
    void load(const Json& input) {
        scene.restore_snapshot(input);
        physics->synchronize(0);
        simulation.reset_presentation();
    }
    void tick(unsigned count = 1) {
        for (unsigned i = 0; i < count; ++i)
            simulation.tick(1.f / 60);
    }
    flecs::entity entity() { return scene.entity(actor); }
};
void shapes_and_domains() {
    for (unsigned shape = 0; shape < 3; ++shape) {
        Fixture test;
        require(!test.physics->status().at("native_2d_realized").get<bool>(),
                "Empty physics created the 2D solver");
        auto input = test.source();
        auto& components = input["entities"][1]["components"];
        if (shape) {
            components.erase("forge.box_collider_2d");
            if (shape == 1)
                components["forge.circle_collider_2d"] = {{"radius", .5}};
            else
                components["forge.capsule_collider_2d"] = {{"radius", .25}, {"height", 1}};
        }
        test.load(input);
        require(test.physics->status().at("bodies_2d") == 2 &&
                    test.physics->status().at("bodies") == 0,
                "2D bodies were missing or projected into the 3D domain");
        test.tick(30);
        require(test.entity().get<LocalTranslation>().y < 4, "2D Dynamic body did not fall");
        test.tick(270);
        const auto position = test.entity().get<LocalTranslation>();
        const double resting = shape == 2 ? .75 : .5;
        require(std::abs(position.y - resting) < .04, "2D collider fell through the solid floor");
        require(position.z == 7 && test.entity().get<LocalScale>() == LocalScale{-1, 1, 0},
                "2D adoption changed authored depth or signed scale");
        Fixture recovered;
        recovered.scene.restore_snapshot(test.scene.snapshot());
        recovered.physics->restore(test.physics->checkpoint());
        require(recovered.physics->body_state_2d(recovered.scene.reference(test.actor))->active ==
                    test.physics->body_state_2d(test.scene.reference(test.actor))->active,
                "2D recovery changed the resting body's sleep state");
    }
    Fixture mixed;
    auto input = mixed.source();
    auto remote = row(EntityId::generate().str(), "3D body", 0, 5, 2);
    auto& components = remote["components"];
    components.erase("forge.physics_body_2d");
    components.erase("forge.box_collider_2d");
    components["forge.local_translation"]["z"] = 100;
    components["forge.local_scale"] = {{"x", 1}, {"y", 1}, {"z", 1}};
    components["forge.physics_body"] = {{"motion", 2},      {"density", 1000},
                                        {"mass", 0},        {"friction", .5},
                                        {"restitution", 0}, {"gravity_factor", 1}};
    components["forge.box_collider"] = {{"x", 1}, {"y", 1}, {"z", 1}};
    const auto remote_id = remote.at("id").get<std::string>();
    input["entities"].push_back(remote);
    mixed.load(input);
    mixed.tick(120);
    require(mixed.entity().get<LocalTranslation>().y > .45 &&
                mixed.scene.entity(remote_id).get<LocalTranslation>().y < -10,
            "3D body collided with the 2D floor or stopped simulating");
}
void settings_and_ownership() {
    Fixture sensor;
    auto source = sensor.source();
    source["entities"][0]["components"]["forge.physics_body_2d"]["sensor"] = true;
    sensor.load(source);
    sensor.tick(100);
    require(sensor.entity().get<LocalTranslation>().y < -3, "2D sensor became solid");
    Fixture mask;
    source = mask.source();
    source["entities"][1]["components"]["forge.physics_body_2d"]["mask"] = 0;
    mask.load(source);
    mask.tick(100);
    require(mask.entity().get<LocalTranslation>().y < -3, "2D collision mask was ignored");
    Fixture locked;
    source = locked.source();
    source["entities"][1]["components"]["forge.physics_body_2d"]["lock_y"] = true;
    locked.load(source);
    locked.tick(60);
    require(std::abs(locked.entity().get<LocalTranslation>().y - 5) < 1e-8,
            "2D Y lock was ignored");
    auto position = locked.entity().get<LocalTranslation>();
    position.z = -9;
    locked.entity().set<LocalTranslation>(position);
    locked.tick();
    require(locked.entity().get<LocalTranslation>().z == -9, "Dynamic 2D prevented a depth edit");
    const auto last = locked.entity().get<LocalTranslation>();
    position.x += 1;
    locked.entity().set<LocalTranslation>(position);
    rejects([&] { locked.physics->synchronize(1.f / 60); });
    locked.entity().set<LocalTranslation>(last);
    locked.tick();
    auto value = locked.entity().get<PhysicsBody2D>();
    value.enabled = false;
    locked.entity().set<PhysicsBody2D>(value);
    locked.physics->synchronize(0);
    require(locked.physics->status().at("bodies_2d") == 1, "Disabled 2D body remained realized");
}
void reject_before_retirement() {
    Fixture test;
    test.load(test.source());
    const auto initial = test.physics->status();
    auto floor = test.scene.entity(test.floor);
    auto value = floor.get<PhysicsBody2D>();
    value.enabled = false;
    floor.set<PhysicsBody2D>(value);
    test.entity().set<LocalRotation>(rotation_about_axis({1, 0, 0}, 10));
    rejects([&] { test.physics->synchronize(0); });
    require(test.physics->status() == initial, "Rejected planar admission retired a prior body");
    floor.set<PhysicsBody2D>({});
    test.entity().set<LocalRotation>({});
    test.entity().set<PhysicsBody>({});
    test.entity().set<BoxCollider>({});
    rejects([&] { test.physics->synchronize(0); });
    require(test.physics->status() == initial, "Mixed simulation writers damaged the native world");
    test.entity().remove<PhysicsBody>();
    test.entity().remove<BoxCollider>();
    test.tick();
}
void kinematic_limits_and_rotation_mapping() {
    Fixture test;
    auto config = PhysicsConfig{};
    config.gravity = {};
    test.physics->configure(config);
    auto source = test.source();
    auto actor = source["entities"][1];
    actor["components"]["forge.local_translation"] = {{"x", 0}, {"y", 0}, {"z", 7}};
    const LocalRotation starting{0, 0, float(std::sin(.05)), float(std::cos(.05))};
    actor["components"]["forge.local_rotation"] = {
        {"x", 0}, {"y", 0}, {"z", starting.z}, {"w", starting.w}};
    source["entities"] = Json::array({actor});
    test.load(source);
    auto check_rotation = [&] {
        const auto expected = affine_transform({{}, starting, {}});
        const auto actual = affine_transform({{}, test.entity().get<LocalRotation>(), {}});
        for (unsigned index : {0u, 1u, 4u, 5u})
            require(std::abs(actual.m[index] - expected.m[index]) < 2e-7,
                    "2D native quaternion mapping introduced angular drift");
    };
    test.physics->adopt();
    check_rotation();
    auto settings = test.entity().get<PhysicsBody2D>();
    settings.mass = 2;
    test.entity().set<PhysicsBody2D>(settings);
    test.physics->synchronize(0);
    test.physics->adopt();
    check_rotation(); // Same-mode native recreation must preserve the actual rotation.
    settings.motion = 1;
    test.entity().set<PhysicsBody2D>(settings);
    test.physics->synchronize(0);
    test.entity().set<LocalTranslation>({1, 0, 7});
    test.tick();
    const auto before = test.physics->status();
    test.entity().set<LocalTranslation>({20, 0, 7});
    rejects([&] { test.physics->synchronize(1.f / 60); });
    require(test.physics->status() == before, "Unreachable Kinematic target changed native status");
    test.entity().set<LocalTranslation>({1, 0, 7});
    test.entity().set<LocalRotation>(rotation_about_axis({0, 0, 1}, 150));
    rejects([&] { test.physics->synchronize(1.f / 60); });
    test.entity().set<LocalRotation>(starting);
    test.tick();
}
void authored_controller_ownership() {
    for (unsigned command = 0; command != 2; ++command) {
        Fixture test;
        auto input = test.source();
        auto& components = input["entities"][1]["components"];
        components.erase("forge.physics_body_2d");
        components.erase("forge.box_collider_2d");
        components["forge.character_controller_2d"] = Json::object();
        components["forge.local_translation"]["y"] = 2;
        if (!command) {
            test.scene.restore_snapshot(input);
            const auto before = test.scene.snapshot();
            auto nodes = test.engine.world().transform_nodes();
            nodes.at(test.entity().id()).local.translation.x += 1;
            rejects([&] { test.physics->validate_transform_candidate(nodes); });
            require(
                test.scene.snapshot() == before &&
                    !test.physics->status().at("native_2d_realized").get<bool>(),
                "Controller animation preflight wrote a starting pose or created a native world");
        }
        test.load(input);
        test.tick(10);
        const auto ref = test.scene.reference(test.actor);
        require(test.entity().get<LocalTranslation>() == LocalTranslation{0, 2, 7} &&
                    !test.entity().has<PhysicsBody2D>() && !test.physics->body_state_2d(ref) &&
                    !test.physics->point_velocity_2d(ref, {0, 2}),
                "Controller acquired authored body data, rigid-body API state or hidden gravity");
        const auto hit = test.physics->raycast_2d({-2, 2.6}, {4, 0});
        require(hit && hit->entity == ref && std::abs(hit->fraction - .425) < 1e-4,
                "Controller inner capsule lost ordinary query presence or feet-origin geometry");
        const auto before = test.scene.snapshot();
        auto nodes = test.engine.world().transform_nodes();
        nodes.at(test.entity().id()).local.translation.x += 1;
        rejects([&] { test.physics->validate_transform_candidate(nodes); });
        require(test.scene.snapshot() == before, "Animation acquired an active controller pose");
        const auto checkpoint = test.physics->checkpoint();
        Fixture recovered;
        recovered.scene.restore_snapshot(before);
        recovered.physics->restore(checkpoint);
        recovered.simulation.reset_presentation();
        recovered.tick();
        require(recovered.scene.snapshot() == before && !recovered.physics->body_state_2d(ref) &&
                    recovered.physics->raycast_2d({-2, 2.6}, {4, 0})->entity == ref,
                "Typed controller source/recovery lost pose, query binding or representation");
        if (command == 0)
            test.physics->teleport_2d(ref, {5, 2}, 0, true);
        else
            test.physics->move_kinematic_2d(ref, {5, 2}, 0);
        rejects([&] { test.physics->synchronize(1.f / 60); });
        require(test.scene.snapshot() == before &&
                    test.physics->checkpoint().at("planar") == checkpoint.at("planar"),
                "Body command changed a controller pose or native state before rejection");
    }
}
void controller_service_motion_and_placement() {
    Fixture test;
    auto source = test.source();
    auto& components = source["entities"][1]["components"];
    components.erase("forge.physics_body_2d");
    components.erase("forge.box_collider_2d");
    components["forge.character_controller_2d"] = Json::object();
    components["forge.local_translation"]["y"] = 0;
    auto wall = row(EntityId::generate().str(), "Wall", 2.5, 2, 0);
    wall["components"]["forge.box_collider_2d"] = {{"x", 1}, {"y", 6}};
    source["entities"].push_back(wall);
    auto spatial = row(EntityId::generate().str(), "Independent falling 3D body", 30, 30, 0);
    spatial["components"].erase("forge.physics_body_2d");
    spatial["components"].erase("forge.box_collider_2d");
    spatial["components"]["forge.local_scale"] = {{"x", 1}, {"y", 1}, {"z", 1}};
    source["entities"].push_back(spatial);
    test.load(source);
    PhysicsBody falling;
    falling.motion = 2;
    test.scene.entity(spatial.at("id").get<std::string>())
        .set<PhysicsBody>(falling)
        .set<BoxCollider>({});
    test.physics->synchronize(0);
    const auto ref = test.scene.reference(test.actor), floor = test.scene.reference(test.floor);
    CharacterService& service = *test.physics;
    const auto initial = service.character_2d(ref);
    require(initial.ground == CharacterGround::OnGround && initial.supporting_entity == floor &&
                initial.supporting_epoch == test.physics->body_state_2d(floor)->epoch &&
                initial.epoch != 0 && initial.ground_normal[1] > .9999,
            "Copied controller service lost initial canonical support state");
    service.move_character_2d(ref, {3, -1});
    require(service.character_2d(ref).position == initial.position,
            "Queued velocity moved a controller immediately");
    test.tick(120);
    auto state = service.character_2d(ref);
    require(std::abs(state.position[0] - 1.705) < 1e-4 &&
                std::abs(state.position[1] + .005) < 1e-4 &&
                std::hypot(state.velocity[0], state.velocity[1]) < 1e-4 &&
                state.epoch == initial.epoch && state.ground == CharacterGround::OnGround,
            "Service motion lost persistent intent, wall/floor clipping or controller epoch");
    service.move_character_2d(ref, {0, 3});
    test.tick(10);
    require(service.character_2d(ref).ground == CharacterGround::InAir &&
                service.character_2d(ref).position[1] > .49,
            "Explicit upward service velocity did not leave ground");
    service.move_character_2d(ref, {0, -3});
    test.tick(20);
    const auto before = service.character_2d(ref);
    service.place_character_2d(ref, {2.5, 0}, true);
    test.physics->synchronize(0);
    state = service.character_2d(ref);
    require(
        state.placement_blocked && state.position == before.position &&
            state.velocity == before.velocity,
        "Blocked queued placement changed current pose/motion instead of reporting copied status");
    service.place_character_2d(ref, {-2, 2}, true);
    test.tick();
    state = service.character_2d(ref);
    require(state.position == Double2{-2, 2} && state.velocity == Double2{} &&
                !state.placement_blocked && test.entity().get<LocalTranslation>().z == 7,
            "Clear queued placement retained motion/status or changed Z");
    service.move_character_2d(ref, {2, 0});
    service.place_character_2d(ref, {-1, 2}, true);
    service.move_character_2d(ref, {-3, 0});
    test.tick();
    state = service.character_2d(ref);
    if (!(std::abs(state.position[0] - (-1 - 3. * float(1.f / 60))) < 1e-6 &&
          state.position[1] == 2 && state.velocity == Double2{-3, 0}))
        throw std::runtime_error(
            "Controller FIFO did not preserve movement/place/clear/movement order: " +
            Json{{"position", state.position},
                 {"velocity", state.velocity},
                 {"blocked", state.placement_blocked}}
                .dump());
    rejects([&] { service.move_character(ref, {}); });
    rejects([&] { (void)service.character(ref); });
    rejects([&] { (void)service.character_2d(floor); });
    rejects([&] { service.move_character_2d(floor, {}); });
    rejects([&] { service.move_character_2d(ref, {1000001, 0}); });
    service.move_character_2d(ref, {1000000, 0});
    test.physics->synchronize(1.f / 60);
    const auto before_step = test.physics->checkpoint();
    rejects([&] { test.physics->step(1.f / 60); });
    require(test.physics->checkpoint() == before_step,
            "Out-of-profile controller displacement advanced a native domain before rejection");
    service.move_character_2d(ref, {});
    test.tick();
    const auto snapshot = test.scene.snapshot();
    service.place_character_2d(ref, {0, 1000000}, false); // Feet fit, centered query does not.
    rejects([&] { test.physics->synchronize(1.f / 60); });
    require(test.scene.snapshot() == snapshot,
            "Out-of-profile controller target wrote before admission");
}
void controller_shared_queue() {
    Fixture test;
    auto source = test.source();
    auto& components = source["entities"][1]["components"];
    components.erase("forge.physics_body_2d");
    components.erase("forge.box_collider_2d");
    components["forge.character_controller_2d"] = Json::object();
    components["forge.local_translation"]["y"] = 0;
    const auto other_id = EntityId::generate().str();
    auto other = row(other_id, "3D character", 30, 0, 0);
    other["components"].erase("forge.physics_body_2d");
    other["components"].erase("forge.box_collider_2d");
    other["components"]["forge.local_scale"] = {{"x", 1}, {"y", 1}, {"z", 1}};
    source["entities"].push_back(other);
    test.scene.restore_snapshot(source);
    test.scene.entity(other_id).set<CharacterController>({});
    test.physics->synchronize(0);
    test.simulation.reset_presentation();
    const auto ref = test.scene.reference(test.actor), spatial = test.scene.reference(other_id);
    test.physics->move_character(spatial, {});
    for (unsigned i = 0; i < 4095; ++i)
        test.physics->move_character_2d(ref, {});
    rejects([&] { test.physics->move_character_2d(ref, {}); });
    rejects([&] { test.physics->move_character(spatial, {}); });
    require(test.physics->checkpoint().at("character_commands").size() == 4096,
            "2D and3D characters did not share the existing4096command queue");
    test.tick();
    require(test.physics->checkpoint().at("character_commands").empty(),
            "Shared character FIFO was not consumed once");
    rejects([&] { (void)test.physics->character_2d(spatial); });
    require(test.physics->character(spatial).ground == CharacterGround::InAir,
            "2D requests changed the separate3D controller meaning");
}
void bounded_work_fault() {
    for (unsigned profile : {0u, 1u, 2u}) {
        Fixture test;
        Json entities = Json::array();
        for (unsigned i = 0; i < 100; ++i) {
            auto body = row(EntityId::generate().str(), "Dense body", 0, 0, 2);
            body["components"]["forge.physics_body_2d"]["sensor"] = profile == 2;
            body["components"]["forge.physics_body_2d"]["bullet"] = profile == 1;
            entities.push_back(std::move(body));
        }
        test.load({{"version", 5}, {"asset_id", AssetId::generate()}, {"entities", entities}});
        const auto before = test.scene.snapshot();
        rejects([&] { test.physics->step(1.f / 60); });
        rejects([&] { test.physics->adopt(); });
        rejects([&] { test.physics->step(1.f / 60); });
        require(test.scene.snapshot() == before,
                "Overflow adopted or continued a partial native step");
    }
}
} // namespace
int main() {
    try {
        shapes_and_domains();
        settings_and_ownership();
        reject_before_retirement();
        kinematic_limits_and_rotation_mapping();
        authored_controller_ownership();
        controller_service_motion_and_placement();
        controller_shared_queue();
        std::cout << "2D CharacterService motion/placement/status/sharedFIFO PASS\n";
        bounded_work_fault();
        std::cout << "2D fixed-phase bodies, domain separation, settings, writer admission and "
                     "work-fault checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
