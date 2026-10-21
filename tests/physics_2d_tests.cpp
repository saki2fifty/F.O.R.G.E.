#include <cmath>
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
        bounded_work_fault();
        std::cout << "2D fixed-phase bodies, domain separation, settings, writer admission and "
                     "work-fault checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
