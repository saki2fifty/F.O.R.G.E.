#include <cmath>
#include <forge/runtime.hpp>
#include <iostream>
#include <numbers>
#include <tuple>
#include <vector>

using namespace forge;
namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
void near(double value, double expected, double tolerance, const char* message) {
    if (!std::isfinite(value) || std::abs(value - expected) > tolerance)
        throw std::runtime_error(std::string(message) + ": actual=" + std::to_string(value) +
                                 " expected=" + std::to_string(expected) +
                                 " tolerance=" + std::to_string(tolerance));
}
template <class Operation> void rejects(Operation&& operation) {
    try {
        operation();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid connected-body candidate was accepted");
}
void reseal(Json& value) {
    value.erase("integrity");
    std::uint64_t hash = 14695981039346656037ull;
    for (unsigned char byte : value.dump()) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    value["integrity"] = std::to_string(hash);
}
struct Fixture {
    EngineContext engine{WorldRole::Runtime, false, {physics_module()}};
    Scene scene{engine.world()};
    RuntimeSimulation simulation{engine.world(), scene};
    std::shared_ptr<PhysicsRuntime> physics =
        std::static_pointer_cast<PhysicsRuntime>(engine.services().physics());
    std::string a = EntityId::generate().str(), b = EntityId::generate().str(),
                joint = EntityId::generate().str(), other_joint = EntityId::generate().str();
    explicit Fixture(double gravity = 0) {
        PhysicsConfig config;
        config.gravity = {0, gravity, 0};
        physics->configure(config);
    }
    void setup(unsigned kind, double x, double y) {
        auto body = [](const std::string& id, double x, double y, unsigned motion) {
            return Json{{"id", id},
                        {"name", motion ? "Body" : "Anchor"},
                        {"spatial", {{"mode", "world"}}},
                        {"components",
                         {{"forge.local_translation", {{"x", x}, {"y", y}, {"z", 9}}},
                          {"forge.local_scale", {{"x", -1}, {"y", 1}, {"z", 0}}},
                          {"forge.physics_body_2d", {{"motion", motion}}},
                          {"forge.circle_collider_2d", {{"radius", .25}}}}}};
        };
        scene.restore_snapshot(
            {{"version", 5},
             {"asset_id", AssetId::generate()},
             {"entities",
              Json::array({body(a, 0, 0, 0),
                           body(b, x, y, 2),
                           {{"id", joint}, {"name", "Connection"}, {"components", Json::object()}},
                           {{"id", other_joint},
                            {"name", "Second connection"},
                            {"components", Json::object()}}})}});
        PhysicsJoint2D value;
        value.kind = kind;
        value.body_a = scene.reference(a);
        value.body_b = scene.reference(b);
        value.collide_connected = false;
        set(value);
    }
    void set(const PhysicsJoint2D& value) { scene.entity(joint).set<PhysicsJoint2D>(value); }
    PhysicsJoint2D get() { return scene.entity(joint).get<PhysicsJoint2D>(); }
    PhysicsBodyState2D state() { return *physics->body_state_2d(scene.reference(b)); }
    void tick(unsigned count = 1) {
        for (unsigned i = 0; i < count; ++i)
            simulation.tick(1.f / 60);
    }
    void recover(Fixture& old) {
        a = old.a;
        b = old.b;
        joint = old.joint;
        other_joint = old.other_joint;
        // New ECS IDs/order must not change native joint recovery bindings.
        for (unsigned i = 0; i < 37; ++i)
            engine.world().world().entity();
        scene.restore_snapshot(old.scene.snapshot());
        physics->restore(old.physics->checkpoint());
        simulation.reset_presentation();
    }
};
void distances() {
    for (bool spring : {false, true}) {
        Fixture test(-10);
        test.setup(0, 0, spring ? -3 : -2);
        auto value = test.get();
        value.distance = 2;
        value.spring_frequency = spring ? 2.f : 0.f;
        test.set(value);
        test.physics->synchronize(0);
        test.tick(600);
        const double extension = spring ? 10 / std::pow(4 * std::numbers::pi, 2) : 0;
        near(test.state().position[1], -2 - extension, .015,
             "2D distance/spring differs from independent static equilibrium");
        require(test.scene.entity(test.b).get<LocalTranslation>().z == 9 &&
                    test.scene.entity(test.b).get<LocalScale>() == LocalScale{-1, 1, 0},
                "Connected simulation rewrote depth or signed scale");
    }
}
void hinge_frames_and_sleep() {
    Fixture pendulum(-10);
    pendulum.setup(1, 2, 0);
    auto value = pendulum.get();
    value.anchor_b_x = 1;
    // Downward is the rest frame; limits then admit the natural half-circle
    // swing instead of stopping its peak velocity at the bottom. Motor hard
    // stops have a separate test below. Native equivalent measures <0.001m here.
    value.frame_b_angle = 90;
    pendulum.set(value);
    pendulum.scene.entity(pendulum.b).set<LocalScale>({-2, 2, 0});
    pendulum.physics->synchronize(0);
    for (unsigned i = 0; i < 120; ++i) {
        pendulum.tick();
        const auto state = pendulum.state();
        near(state.position[0] - 2 * std::cos(state.angle), 0, .003,
             "Signed-scale hinge anchor drifted in X");
        near(state.position[1] - 2 * std::sin(state.angle), 0, .003,
             "Signed-scale hinge anchor drifted in Y");
    }
    Fixture motor;
    motor.setup(1, 0, 0);
    value = motor.get();
    value.motor_enabled = true;
    value.min_angle = -30;
    value.max_angle = 30;
    motor.set(value);
    motor.physics->synchronize(0);
    motor.tick(300);
    near(motor.state().angle, std::numbers::pi / 6, .015, "Hinge upper limit/motor units failed");
    require(!motor.state().active, "Hinge resting at its limit never slept");
    value.slider_motor_speed = 17; // Inactive in Hinge: do not wake or rebuild.
    motor.set(value);
    motor.physics->synchronize(0);
    motor.tick();
    require(!motor.state().active, "Inactive joint edit woke a sleeping body");
    value.hinge_motor_speed = -90;
    motor.set(value);
    motor.tick(300);
    near(motor.state().angle, -std::numbers::pi / 6, .015,
         "Editing a sleeping hinge's motor did not reverse it");
}
void sliders() {
    Fixture test;
    test.setup(2, 0, 0);
    auto value = test.get();
    value.frame_a_angle = value.frame_b_angle = 90;
    value.motor_enabled = true;
    value.min_travel = -.5f;
    value.max_travel = .5f;
    test.set(value);
    test.physics->synchronize(0);
    test.tick(15);
    near(test.state().position[1], .25, .02, "Slider frame/motor direction failed");
    near(test.state().position[0], 0, 1e-5, "Slider left its rotated frame axis");
    test.tick(285);
    near(test.state().position[1], .5, .015, "Slider upper stop failed");
    value.slider_motor_speed = -1;
    test.set(value);
    test.tick(300);
    near(test.state().position[1], -.5, .015, "Slider reverse/lower stop failed");
}
void connected_contacts() {
    Fixture test;
    test.setup(0, .3, 0);
    auto value = test.get();
    value.distance = .3f;
    value.collide_connected = true;
    test.set(value);
    test.physics->synchronize(0);
    test.tick();
    require(test.physics->contact_count_2d() == 1 && test.physics->contact_event_2d(0)->begin,
            "Connected permitted contact missing");
    const auto epoch = test.state().epoch;
    value.collide_connected = false;
    test.set(value);
    test.physics->synchronize(0);
    rejects([&] { (void)test.physics->checkpoint(); });
    test.tick();
    require(test.physics->contact_count_2d() == 1 && !test.physics->contact_event_2d(0)->begin &&
                test.state().epoch == epoch,
            "Joint exclusion lost End or rebuilt the body epoch");
    test.scene.entity(test.other_joint).set<PhysicsJoint2D>(value);
    test.tick();
    require(test.physics->contact_count_2d() == 0, "Duplicate exclusion fabricated an event");
    test.scene.entity(test.joint).remove<PhysicsJoint2D>();
    test.tick();
    require(test.physics->contact_count_2d() == 0, "Retiring one exclusion bypassed another");
    test.scene.entity(test.other_joint).remove<PhysicsJoint2D>();
    test.tick();
    require(test.physics->contact_count_2d() == 1 && test.physics->contact_event_2d(0)->begin &&
                test.state().epoch == epoch,
            "Retiring the last exclusion did not restore the connected contact");
}
void admission_and_replacement() {
    Fixture test;
    test.setup(0, 0, -2);
    auto value = test.get();
    value.distance = 2;
    test.set(value);
    test.physics->synchronize(0);
    test.tick(30);
    const auto before = test.state();
    const auto original = value;
    value.body_b = value.body_a;
    test.set(value);
    rejects([&] { test.physics->synchronize(0); });
    require(test.state() == before, "Invalid joint changed native realization");
    test.set(original);
    auto body = test.scene.entity(test.b).get<PhysicsBody2D>();
    body.sensor = true;
    test.scene.entity(test.b).set<PhysicsBody2D>(body);
    rejects([&] { test.physics->synchronize(0); });
    require(test.state() == before, "Invalid sensor/joint candidate replaced a live body");
    body.sensor = false;
    body.mass = 2;
    test.scene.entity(test.b).set<PhysicsBody2D>(body);
    test.physics->synchronize(0);
    require(test.state().epoch != before.epoch, "Mass edit did not replace the body");
    test.tick(60);
    near(std::hypot(test.state().position[0], test.state().position[1]), 2, .02,
         "Joint did not rebind after body replacement");
}
void kinematic_anchors_and_locked_bodies() {
    for (unsigned kind : {0u, 1u, 2u}) {
        Fixture moving;
        moving.setup(kind, 0, kind == 0 ? -2 : 0);
        auto joint = moving.get();
        joint.distance = 2;
        moving.set(joint);
        auto anchor = moving.scene.entity(moving.a).get<PhysicsBody2D>();
        anchor.motion = 1;
        moving.scene.entity(moving.a).set(anchor);
        moving.physics->synchronize(0);
        const auto epoch = moving.state().epoch;
        for (unsigned tick = 1; tick <= 120; ++tick) {
            moving.physics->move_kinematic_2d(moving.scene.reference(moving.a), {0, tick * .005},
                                              0);
            moving.tick();
            const auto a = moving.physics->body_state_2d(moving.scene.reference(moving.a));
            const auto b = moving.state();
            require(a && a->motion == 1 && b.epoch == epoch,
                    "Connected Kinematic movement replaced or lost its body");
            near(b.position[1] - a->position[1], kind == 0 ? -2 : 0, .005,
                 "Joint did not follow its admitted moving anchor");
        }
        near(moving.state().position[1], kind == 0 ? -1.4 : .6, .005,
             "Connected Kinematic target did not move the Dynamic participant");

        Fixture locked;
        locked.setup(kind, 0, kind == 0 ? -2 : 0);
        joint = locked.get();
        joint.distance = 2;
        joint.motor_enabled = true; // Inactive for Distance; active and bounded otherwise.
        locked.set(joint);
        auto body = locked.scene.entity(locked.b).get<PhysicsBody2D>();
        body.lock_x = body.lock_y = body.lock_rotation = true;
        locked.scene.entity(locked.b).set(body);
        locked.physics->synchronize(0);
        const auto before = locked.state();
        locked.tick(120);
        near(locked.state().position[0], before.position[0], 1e-7,
             "Joint drive bypassed the body's X lock");
        near(locked.state().position[1], before.position[1], 1e-7,
             "Joint drive bypassed the body's Y lock");
        near(locked.state().angle, before.angle, 1e-7,
             "Joint drive bypassed the body's rotation lock");
    }
}
void combined_capacity() {
    Fixture test;
    test.setup(0, 1, 0);
    auto source = test.scene.snapshot();
    const auto a = EntityId::generate().str(), b = EntityId::generate().str();
    for (const auto& [id, x, motion] : {std::tuple{a, 100, 0}, std::tuple{b, 101, 2}})
        source["entities"].push_back(
            {{"id", id},
             {"name", "Spatial body"},
             {"spatial", {{"mode", "world"}}},
             {"components",
              {{"forge.local_translation", {{"x", x}, {"y", 0}, {"z", 0}}},
               {"forge.physics_body",
                {{"motion", motion},
                 {"density", 1000},
                 {"mass", 0},
                 {"friction", .5},
                 {"restitution", 0},
                 {"gravity_factor", 1}}},
               {"forge.box_collider", {{"x", .5}, {"y", .5}, {"z", .5}}}}}});
    std::vector<std::string> ids;
    for (unsigned i = 0; i < 4096; ++i) {
        ids.push_back(EntityId::generate().str());
        source["entities"].push_back(
            {{"id", ids.back()}, {"name", "Spatial connection"}, {"components", Json::object()}});
    }
    test.scene.restore_snapshot(source);
    PhysicsJoint value;
    value.body_a = test.scene.reference(a);
    value.body_b = test.scene.reference(b);
    value.enabled = false;
    for (const auto& id : ids)
        test.scene.entity(id).set<PhysicsJoint>(value);
    test.physics->synchronize(0);
    const auto before = test.state();
    const auto spatial_before = test.physics->body_state(test.scene.reference(b));
    value.enabled = true;
    for (const auto& id : ids)
        test.scene.entity(id).set<PhysicsJoint>(value);
    rejects([&] { test.physics->synchronize(0); });
    require(test.state() == before &&
                test.physics->body_state(test.scene.reference(b)) == spatial_before &&
                test.physics->status().at("joints") == 0 &&
                test.physics->status().at("joints_2d") == 1,
            "Combined joint overflow mutated a native domain");
    value.enabled = false;
    test.scene.entity(ids.back()).set<PhysicsJoint>(value);
    test.physics->synchronize(0);
    require(test.physics->status().at("joints") == 4095 &&
                test.physics->status().at("joints_2d") == 1,
            "Shared4096-joint capacity rejected an admitted mixed-domain boundary");
    auto planar = test.get();
    planar.enabled = false;
    test.set(planar);
    value.enabled = true;
    test.scene.entity(ids.back()).set<PhysicsJoint>(value);
    test.physics->synchronize(0);
    require(test.physics->status().at("joints") == 4096 &&
                test.physics->status().at("joints_2d") == 0,
            "2D integration reduced the existing3D joint boundary");
    planar.enabled = true;
    test.set(planar);
    rejects([&] { test.physics->synchronize(0); });
    require(test.physics->status().at("joints") == 4096 &&
                test.physics->status().at("joints_2d") == 0,
            "Rejected planar overflow changed the full spatial realization");
}
void recovery() {
    for (unsigned kind = 0; kind < 3; ++kind) {
        Fixture original;
        original.setup(kind, kind == 0 ? 2 : 0, 0);
        auto value = original.get();
        value.distance = 2;
        value.limits_enabled = false;
        value.motor_enabled = true;
        value.hinge_motor_speed = 45;
        value.slider_motor_speed = .2f;
        original.set(value);
        original.scene.entity(original.other_joint).set<PhysicsJoint2D>(value);
        original.physics->synchronize(0);
        original.tick(30);
        Fixture recovered;
        recovered.recover(original);
        require(recovered.physics->contact_count_2d() == 0, "Recovery replayed a contact event");
        require(recovered.state().epoch == original.state().epoch, "Recovery changed body epoch");
        const auto before = recovered.state();
        const auto checkpoint = original.physics->checkpoint();
        for (unsigned corruption = 0; corruption < 3; ++corruption) {
            auto invalid = checkpoint;
            auto& entries = invalid["planar"]["metadata"]["joints"];
            if (corruption == 0)
                entries[1] = entries[0];
            else if (corruption == 1)
                entries[0]["anchor_b"][0] = 10000;
            else
                entries[0]["configuration"]["distance"] = 3;
            reseal(invalid);
            rejects([&] { recovered.physics->restore(invalid); });
            require(recovered.state() == before && recovered.physics->status().at("joints_2d") == 2,
                    "Rejected recovery changed the published connected-body world");
        }
        original.scene.entity(original.other_joint).remove<PhysicsJoint2D>();
        recovered.scene.entity(recovered.other_joint).remove<PhysicsJoint2D>();
        for (unsigned i = 0; i < 120; ++i) {
            original.tick();
            recovered.tick();
            const auto a = original.state(), b = recovered.state();
            near(a.position[0], b.position[0], 1e-6, "Recovered joint continuation changed X");
            near(a.position[1], b.position[1], 1e-6, "Recovered joint continuation changed Y");
            near(a.angle, b.angle, 1e-6, "Recovered joint continuation changed angle");
        }
    }
}
} // namespace
int main() {
    try {
        distances();
        hinge_frames_and_sleep();
        sliders();
        connected_contacts();
        admission_and_replacement();
        kinematic_anchors_and_locked_bodies();
        combined_capacity();
        recovery();
        std::cout << "2D joints: independent equations/frames, motors/sleep, contact retirement, "
                     "atomic admission/replacement and fresh duplicate-joint recovery passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
