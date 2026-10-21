#include <cmath>
#include <forge/runtime.hpp>
#include <iostream>
#include <thread>

using namespace forge;
namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
void near(double actual, double expected, const char* message, double tolerance = 2e-5) {
    require(std::abs(actual - expected) <= tolerance, message);
}
template <class F> void rejects(F&& operation) {
    try {
        operation();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid 2D control was accepted");
}
struct Fixture {
    EngineContext engine{WorldRole::Runtime, false, {physics_module()}};
    Scene scene{engine.world()};
    RuntimeSimulation simulation{engine.world(), scene};
    std::shared_ptr<PhysicsRuntime> physics =
        std::static_pointer_cast<PhysicsRuntime>(engine.services().physics());
    std::string actor = EntityId::generate().str();
    Fixture() {
        auto config = PhysicsConfig{};
        config.gravity = {};
        physics->configure(config);
    }
    Json source(unsigned motion = 2) const {
        const Json components = {{"forge.local_translation", {{"x", 0}, {"y", 0}, {"z", 7}}},
                                 {"forge.local_scale", {{"x", -1}, {"y", 1}, {"z", 0}}},
                                 {"forge.physics_body_2d", {{"motion", motion}}},
                                 {"forge.box_collider_2d", {{"x", 1}, {"y", 1}}}};
        const Json entity = {{"id", actor},
                             {"name", "Controlled body"},
                             {"spatial", {{"mode", "world"}}},
                             {"components", components}};
        return {
            {"version", 5}, {"asset_id", AssetId::generate()}, {"entities", Json::array({entity})}};
    }
    void load(Json source) {
        scene.restore_snapshot(source);
        physics->synchronize(0);
        simulation.reset_presentation();
    }
    flecs::entity entity() { return scene.entity(actor); }
    EntityRef ref() { return scene.reference(actor); }
    PhysicsBodyState2D state() {
        const auto value = physics->body_state_2d(ref());
        require(bool(value), "Missing realized 2D state");
        return *value;
    }
    void tick(unsigned count = 1) {
        for (unsigned i = 0; i < count; ++i)
            simulation.tick(1.f / 60);
    }
};
void loads_and_observations() {
    Fixture test;
    test.scene.restore_snapshot(test.source());
    require(!test.physics->body_state_2d(test.ref()), "Unrealized state was fabricated");
    test.physics->synchronize(0);
    auto state = test.state();
    near(state.mass, 1, "Area density mass is wrong");
    near(state.rotational_inertia, 1. / 6, "Box area inertia is wrong");
    require(state.epoch != 0 && state.motion == 2, "2D state lacks its realization epoch");
    test.physics->add_force_2d(test.ref(), {6, 0});
    require(test.state() == state, "Read flushed a queued 2D force");
    test.tick();
    near(test.state().linear_velocity[0], .1, "One-step 2D force has wrong units");
    test.tick();
    near(test.state().linear_velocity[0], .1, "2D force leaked into the next tick");
    test.physics->add_impulse_2d(test.ref(), {1, 0});
    test.physics->add_torque_2d(test.ref(), 1);
    test.tick();
    near(test.state().linear_velocity[0], 1.1, "2D impulse has wrong mass scaling");
    near(test.state().angular_velocity, .1, "2D torque has wrong inertia scaling");
    test.physics->add_angular_impulse_2d(test.ref(), .2);
    test.physics->synchronize(1.f / 60);
    near(test.state().angular_velocity, 1.3, "2D angular impulse has wrong units");
    test.physics->step(1.f / 60);
    test.physics->adopt();
    const auto queued_before = test.state();
    test.physics->teleport_2d(test.ref(), {100, 50}, .2, true);
    test.physics->add_impulse_2d(test.ref(), {0, 2}, Double2{101, 50});
    require(test.state() == queued_before, "Copied state flushed pending placement/impulse");
    test.physics->synchronize(1.f / 60);
    state = test.state();
    near(state.position[0], 100, "2D teleport did not place native origin");
    near(state.position[1], 50, "2D teleport did not place native origin");
    near(state.angle, .2, "2D target angle is not radians");
    near(state.angular_velocity, 12, "Point impulse did not use the preceding FIFO teleport");
    const auto point_velocity = test.physics->point_velocity_2d(test.ref(), {101, 50});
    require(bool(point_velocity), "2D point velocity is missing");
    near((*point_velocity)[0], 0, "2D point velocity X is wrong");
    near((*point_velocity)[1], 14, "2D point velocity omitted angular contribution");
    require(test.entity().get<LocalTranslation>().z == 7 &&
                test.entity().get<LocalScale>() == LocalScale{-1, 1, 0},
            "2D controls changed depth or signed scale");
    test.physics->set_linear_velocity_2d(test.ref(), {3, 4});
    test.physics->set_angular_velocity_2d(test.ref(), 2);
    test.physics->synchronize(1.f / 60);
    state = test.state();
    near(state.linear_velocity[0], 3, "2D velocity channel changed");
    near(state.linear_velocity[1], 4, "2D velocity channel changed");
    near(state.angular_velocity, 2, "2D angular velocity is not radians/s");
    test.physics->set_linear_velocity_2d(test.ref(), {});
    test.physics->set_angular_velocity_2d(test.ref(), 0);
    test.physics->synchronize(1.f / 60);
    near(test.state().linear_velocity[0], 0, "Zero linear velocity failed to stop motion");
    near(test.state().angular_velocity, 0, "Zero angular velocity failed to stop motion");
    // clear_velocity clears velocity, not earlier one-step loads in the FIFO.
    test.physics->add_force_2d(test.ref(), {6, 0});
    test.physics->teleport_2d(test.ref(), {0, 0}, 0, true);
    test.tick();
    near(test.state().linear_velocity[0], .1,
         "2D clear_velocity silently discarded earlier forces");
    rejects([&] { test.physics->set_linear_velocity(test.ref(), {1, 0, 0}); });
    require(!test.physics->body_state(test.ref()), "2D state leaked through the 3D API");
}
void sleeping_and_epochs() {
    Fixture test;
    test.load(test.source());
    test.tick(180);
    const auto asleep = test.state();
    require(!asleep.active, "Settled zero-gravity 2D body did not sleep");
    require(test.physics->point_velocity_2d(test.ref(), {1, 0}) ==
                std::optional<Double2>{Double2{}},
            "Sleeping point observation is wrong");
    require(test.state() == asleep, "2D read woke or changed a sleeping body");
    test.physics->add_force_2d(test.ref(), {});
    test.physics->synchronize(1.f / 60);
    require(test.state() == asleep, "Zero force woke a 2D body");
    test.physics->add_impulse_2d(test.ref(), {.1, 0});
    test.physics->synchronize(1.f / 60);
    require(test.state().active, "Nonzero impulse did not wake a 2D body");
    auto config = test.entity().get<PhysicsBody2D>();
    config.mass = 2;
    test.entity().set<PhysicsBody2D>(config);
    test.physics->synchronize(0);
    const auto replaced = test.state();
    require(replaced.epoch != asleep.epoch, "Replacement reused a native realization epoch");
    near(replaced.mass, 2, "2D mass override was not applied");
    near(replaced.rotational_inertia, 1. / 3,
         "2D mass override did not scale inertia proportionally");
    near(replaced.linear_velocity[0], .1, "Same-mode recreation lost motion");
    const auto owner_ref = test.ref();
    bool wrong_thread_rejected = false;
    std::thread other([&] {
        try {
            (void)test.physics->body_state_2d(owner_ref);
        } catch (const std::exception&) {
            wrong_thread_rejected = true;
        }
    });
    other.join();
    require(wrong_thread_rejected, "2D state escaped the owner-thread guard");
    test.physics->stop();
    rejects([&] { test.physics->body_state_2d(test.ref()); });
}
void planar_parent_targets() {
    for (unsigned motion : {0u, 1u})
        for (float depth_scale : {-3.f, 0.f, 3.f}) {
            Fixture test;
            auto source = test.source(motion);
            const auto parent = EntityId::generate().str();
            source["entities"][0]["parent"] = parent;
            source["entities"][0]["spatial"] = {{"mode", "follow_structure"}};
            source["entities"][0]["components"]["forge.local_translation"] = {
                {"x", 1}, {"y", 2}, {"z", 3}};
            source["entities"].push_back(
                {{"id", parent},
                 {"name", "Reflected flat parent"},
                 {"components",
                  {{"forge.local_translation", {{"x", 10}, {"y", -20}, {"z", 2}}},
                   {"forge.local_rotation",
                    {{"x", 0}, {"y", 0}, {"z", std::sin(.185)}, {"w", std::cos(.185)}}},
                   {"forge.local_scale", {{"x", -2}, {"y", 2}, {"z", depth_scale}}}}}});
            test.load(source);
            test.physics->teleport_2d(test.ref(), {30, 20}, .3, true);
            if (motion == 1)
                test.physics->move_kinematic_2d(test.ref(), {31, 20}, .3);
            test.tick();
            near(test.state().position[0], motion == 1 ? 31 : 30,
                 "Parented 2D target conversion or FIFO anchor failed");
            near(test.state().position[1], 20, "Parented 2D world Y target failed");
            near(test.state().angle, .3, "Reflected parent changed the world rotation target",
                 5e-5);
            require(test.entity().get<LocalTranslation>().z == 3 &&
                        test.entity().get<LocalScale>() == LocalScale{-1, 1, 0},
                    "Parented 2D target changed authored local depth/scale");
            if (motion == 1) {
                test.physics->move_kinematic_2d(test.ref(), {32, 20}, .3);
                test.physics->teleport_2d(test.ref(), {30, 20}, .3, false);
                test.tick();
                near(test.state().position[0], 30,
                     "Last teleport failed to override an earlier Kinematic target");
                near(test.state().linear_velocity[0], 0,
                     "Final teleport anchor retained an obsolete Kinematic velocity");
            }
        }
    Fixture fresh;
    fresh.scene.restore_snapshot(fresh.source(1));
    fresh.physics->teleport_2d(fresh.ref(), {100, 0}, 0, true);
    fresh.physics->move_kinematic_2d(fresh.ref(), {101, 0}, 0);
    fresh.tick();
    near(fresh.state().position[0], 101,
         "First-realization FIFO target ignored its teleport anchor");
    Fixture oversized;
    oversized.scene.restore_snapshot(oversized.source(1));
    oversized.physics->move_kinematic_2d(oversized.ref(), {20, 0}, 0);
    const auto before = oversized.scene.snapshot();
    rejects([&] { oversized.physics->synchronize(1.f / 60); });
    require(oversized.scene.snapshot() == before &&
                !oversized.physics->status().at("native_2d_realized").get<bool>(),
            "Rejected initial Kinematic target wrote the scene or created native state");
}
void combined_fifo_capacity() {
    Fixture test;
    auto source = test.source();
    const auto other = EntityId::generate().str();
    source["entities"].push_back({{"id", other},
                                  {"name", "3D queue participant"},
                                  {"spatial", {{"mode", "world"}}},
                                  {"components",
                                   {{"forge.local_translation", {{"x", 50}, {"y", 50}, {"z", 50}}},
                                    {"forge.physics_body",
                                     {{"motion", 2},
                                      {"density", 1000},
                                      {"mass", 0},
                                      {"friction", .5},
                                      {"restitution", 0},
                                      {"gravity_factor", 1}}},
                                    {"forge.box_collider", {{"x", 1}, {"y", 1}, {"z", 1}}}}}});
    test.load(source);
    const auto ref3d = test.scene.reference(other);
    require(!test.physics->body_state_2d(ref3d), "3D record leaked into the 2D API");
    for (unsigned i = 0; i < 4095; ++i)
        test.physics->set_linear_velocity(ref3d, {});
    test.physics->set_linear_velocity_2d(test.ref(), {1, 0});
    rejects([&] { test.physics->set_angular_velocity_2d(test.ref(), 1); });
    test.tick();
    near(test.state().linear_velocity[0], 1, "Shared queue dropped the admitted 4096th command");
    near(test.state().angular_velocity, 0, "Overflow partially enqueued a rejected command");
}
} // namespace
int main() {
    try {
        loads_and_observations();
        sleeping_and_epochs();
        planar_parent_targets();
        combined_fifo_capacity();
        std::cout << "2D copied state, physical loads, shared FIFO, parent targets and lifetime "
                     "guards passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
