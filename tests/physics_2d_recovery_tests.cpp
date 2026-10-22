#include "physics_2d.hpp"
#include <cmath>
#include <forge/runtime.hpp>
#include <iostream>

using namespace forge;
namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class F> void rejects(F&& operation) {
    try {
        operation();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid 2D recovery was accepted");
}
void reseal(Json& value) {
    value.erase("integrity");
    std::uint64_t hash = 14695981039346656037ull;
    for (unsigned char c : value.dump()) {
        hash ^= c;
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
    std::string actor = EntityId::generate().str();
    Fixture() {
        auto config = PhysicsConfig{};
        config.gravity = {};
        physics->configure(config);
    }
    Json source(unsigned motion = 2, unsigned shape = 0) const {
        Json components = {{"forge.local_translation", {{"x", 999990}, {"y", 2}, {"z", 7}}},
                           {"forge.local_scale", {{"x", -1}, {"y", 1}, {"z", 0}}},
                           {"forge.physics_body_2d",
                            {{"motion", motion},
                             {"mass", 2},
                             {"linear_damping", .2},
                             {"angular_damping", .3},
                             {"bullet", true},
                             {"mask", 0}}}};
        if (shape == 0)
            components["forge.box_collider_2d"] = {{"x", 1}, {"y", 2}};
        else if (shape == 1)
            components["forge.circle_collider_2d"] = {{"radius", .5}};
        else
            components["forge.capsule_collider_2d"] = {{"radius", .5}, {"height", 1}};
        return {{"version", 5},
                {"asset_id", AssetId::generate()},
                {"entities", Json::array({{{"id", actor},
                                           {"name", "Recovery body"},
                                           {"spatial", {{"mode", "world"}}},
                                           {"components", components}}})}};
    }
    void load(const Json& input) {
        scene.restore_snapshot(input);
        physics->synchronize(0);
    }
    void tick(unsigned count = 1) {
        for (unsigned i = 0; i < count; ++i)
            simulation.tick(1.f / 60);
    }
    EntityRef ref() { return scene.reference(actor); }
    PhysicsBodyState2D state() { return *physics->body_state_2d(ref()); }
    void recover(Fixture& original) {
        actor = original.actor;
        scene.restore_snapshot(original.scene.snapshot());
        physics->restore(original.physics->checkpoint());
        simulation.reset_presentation();
    }
};
void equal_state(Fixture& a, Fixture& b) {
    const auto x = a.state(), y = b.state();
    require(x.epoch == y.epoch && x.motion == y.motion && x.active == y.active &&
                x.position == y.position && x.linear_velocity == y.linear_velocity &&
                x.angular_velocity == y.angular_velocity && x.angle == y.angle &&
                x.mass == y.mass && x.rotational_inertia == y.rotational_inertia,
            "Recovered native state/continuation differs");
}
void roundtrips() {
    for (unsigned shape = 0; shape < 3; ++shape)
        for (unsigned motion = 0; motion < 3; ++motion) {
            Fixture original;
            original.load(original.source(motion, shape));
            Fixture initial;
            initial.recover(original); // Also supports the initial zero-tick boundary.
            equal_state(initial, original);
            if (motion == 2) {
                original.physics->set_linear_velocity_2d(original.ref(), {-2, 1});
                original.physics->set_angular_velocity_2d(original.ref(), .7);
            } else if (motion == 1) {
                // The native kinematic API reaches a close, not exact, angular target.
                original.physics->move_kinematic_2d(original.ref(), {999989, 3}, .3);
            }
            original.tick(5);
            Fixture recovered;
            recovered.recover(original);
            equal_state(recovered, original);
            require(recovered.physics->contact_count_2d() == 0, "Recovery replayed adopted events");
            for (unsigned tick = 0; tick < 90; ++tick) {
                original.tick();
                recovered.tick();
                equal_state(recovered, original);
            }
            const auto entity = recovered.scene.entity(original.actor);
            require(entity.get<LocalTranslation>().z == 7 &&
                        entity.get<LocalScale>() == LocalScale{-1, 1, 0},
                    "Recovery changed depth or signed presentation scale");
        }
    Fixture sleeping;
    sleeping.load(sleeping.source());
    sleeping.tick(240);
    require(!sleeping.state().active, "Unforced body never slept");
    Fixture restored;
    restored.recover(sleeping);
    equal_state(restored, sleeping);
    auto body = sleeping.scene.entity(sleeping.actor).get<PhysicsBody2D>();
    body.lock_rotation = body.lock_x = true;
    sleeping.scene.entity(sleeping.actor).set<PhysicsBody2D>(body);
    sleeping.tick();
    Fixture locked;
    locked.recover(sleeping);
    equal_state(locked, sleeping);
}
void queues_and_rejection() {
    Fixture original;
    original.load(original.source());
    original.physics->teleport_2d(original.ref(), {999980, 1}, .2, true);
    original.physics->add_impulse_2d(original.ref(), {4, 0}, Double2{999980, 2});
    original.physics->add_force_2d(original.ref(), {0, 3});
    original.physics->add_torque_2d(original.ref(), .1);
    original.physics->set_angular_velocity_2d(original.ref(), .5);
    Fixture recovered;
    recovered.recover(original);
    original.tick();
    recovered.tick();
    equal_state(recovered, original);
    const auto snapshot = original.scene.snapshot(), valid = original.physics->checkpoint();
    for (unsigned failure = 0; failure < 10; ++failure) {
        auto broken = valid;
        auto& block = broken["planar"];
        if (failure == 0)
            block["metadata"]["pin"] = "wrong";
        if (failure == 1)
            block["metadata"]["mapping"][0]["epoch"] = std::uint64_t{0};
        if (failure == 2)
            block["metadata"]["next_epoch"] = std::uint64_t{1};
        if (failure == 3)
            block["metadata"]["mapping"][0]["target"]["position"][0] = 42;
        if (failure == 4)
            block["metadata"]["mapping"][0]["configuration"]["body"]["mass"] = 3;
        if (failure == 5)
            block["metadata"]["mapping"][0]["native"]["angular"] = 9;
        if (failure == 6)
            block["solver"] = "00";
        if (failure == 7)
            block["bytes"] = 2 * 1024 * 1024;
        auto candidate_source = snapshot;
        if (failure == 8) {
            auto& x = candidate_source["entities"][0]["components"]["forge.local_translation"]["x"];
            x = x.get<double>() + 1;
            block["metadata"]["mapping"][0]["target"]["position"][0] = x;
        }
        if (failure == 9)
            block["metadata"]["mapping"][0]["target"]["position"].push_back(0);
        reseal(broken);
        Fixture candidate;
        candidate.actor = original.actor;
        candidate.scene.restore_snapshot(candidate_source);
        rejects([&] { candidate.physics->restore(broken); });
        require(candidate.physics->status().at("bodies_2d") == 0,
                "Rejected candidate published partial 2D bodies");
        candidate.scene.restore_snapshot(snapshot);
        candidate.physics->restore(valid); // Failed native candidate leaves fresh owner reusable.
        equal_state(candidate, original);
    }
    rejects([&] { recovered.physics->restore(valid); });
    original.tick();
    recovered.tick();
    equal_state(recovered, original);
    auto position = original.scene.entity(original.actor).get<LocalTranslation>();
    position.z = -4;
    original.scene.entity(original.actor).set<LocalTranslation>(position);
    rejects([&] { (void)original.physics->checkpoint(); });
    original.tick();
    Fixture depth;
    depth.recover(original);
    require(depth.scene.entity(original.actor).get<LocalTranslation>().z == -4,
            "Recovered 2D body lost synchronized authored depth");
}
void mixed_queue_and_empty_epoch() {
    Fixture original;
    auto source = original.source();
    auto other = source["entities"][0];
    const auto other_id = EntityId::generate().str();
    other["id"] = other_id;
    auto& components = other["components"];
    components.erase("forge.physics_body_2d");
    components.erase("forge.box_collider_2d");
    components["forge.local_translation"] = {{"x", 0}, {"y", 0}, {"z", 100}};
    components["forge.local_scale"] = {{"x", 1}, {"y", 1}, {"z", 1}};
    components["forge.physics_body"] = {{"motion", 2},      {"density", 1000},
                                        {"mass", 2},        {"friction", .5},
                                        {"restitution", 0}, {"gravity_factor", 1}};
    components["forge.box_collider"] = {{"x", 1}, {"y", 1}, {"z", 1}};
    source["entities"].push_back(other);
    original.load(source);
    const auto ref = original.scene.reference(other_id);
    original.physics->teleport(ref, {2, 1, 100}, {}, true);
    original.physics->teleport_2d(original.ref(), {999980, 1}, .2, true);
    original.physics->add_impulse(ref, {2, 1, 0});
    original.physics->add_impulse_2d(original.ref(), {2, 1});
    const auto checkpoint = original.physics->checkpoint();
    for (unsigned i = 0; i < 4; ++i)
        require(checkpoint["commands"][i]["planar"] == bool(i % 2),
                "Mixed FIFO lost its domain order");
    Fixture restored;
    restored.recover(original);
    for (unsigned i = 0; i < 30; ++i) {
        original.tick();
        restored.tick();
        equal_state(restored, original);
        require(original.physics->body_state(ref) == restored.physics->body_state(ref),
                "3D state diverged after mixed queue recovery");
    }
    for (unsigned failure = 0; failure < 4; ++failure) {
        auto broken = checkpoint;
        if (failure == 0)
            broken["commands"][1]["planar"] = false;
        if (failure == 1)
            broken["commands"][3]["value"][2] = 1;
        if (failure == 2)
            broken["version"] = 4;
        if (failure == 3)
            broken["commands"][1]["position"][0] = 1000001;
        reseal(broken);
        Fixture candidate;
        candidate.scene.restore_snapshot(source);
        rejects([&] { candidate.physics->restore(broken); });
    }
    Fixture empty;
    empty.load(empty.source());
    const auto epoch = empty.state().epoch;
    auto body = empty.scene.entity(empty.actor).get<PhysicsBody2D>();
    body.enabled = false;
    empty.scene.entity(empty.actor).set<PhysicsBody2D>(body);
    empty.physics->synchronize(0);
    rejects([&] { (void)empty.physics->checkpoint(); }); // Retired native bindings not adopted.
    empty.tick();
    Fixture recovered_empty;
    recovered_empty.recover(empty);
    require(recovered_empty.physics->status().at("native_2d_realized") == true &&
                recovered_empty.physics->status().at("bodies_2d") == 0,
            "Recovery discarded a realized empty native world and its epochs");
    body.enabled = true;
    empty.scene.entity(empty.actor).set<PhysicsBody2D>(body);
    recovered_empty.scene.entity(empty.actor).set<PhysicsBody2D>(body);
    empty.tick();
    recovered_empty.tick();
    equal_state(empty, recovered_empty);
    require(empty.state().epoch > epoch, "Empty-world recovery reused a retired epoch");
}
void maximum_sensor_recovery() {
    Fixture original;
    auto source = original.source();
    const auto model = source["entities"][0];
    source["entities"] = Json::array();
    for (unsigned i = 0; i < 1024; ++i) {
        auto row = model;
        row["id"] = EntityId::generate().str();
        auto& components = row["components"];
        components["forge.physics_body_2d"] = {{"motion", i < 4 ? 0 : 2}, {"sensor", i < 4}};
        components["forge.local_translation"] = {{"x", i < 4 ? 0 : double((i - 4) % 32) * 2 - 32},
                                                 {"y", i < 4 ? 0 : double((i - 4) / 32) * 2 - 32},
                                                 {"z", 7}};
        if (i < 4)
            components["forge.box_collider_2d"] = {{"x", 1000}, {"y", 1000}};
        else {
            components.erase("forge.box_collider_2d");
            components["forge.circle_collider_2d"] = {{"radius", .5}};
        }
        source["entities"].push_back(std::move(row));
    }
    original.load(source);
    original.tick();
    require(original.physics->contact_count_2d() == 4092, "Maximum sensor profile did not form");
    Fixture restored;
    restored.recover(original);
    require(restored.physics->contact_count_2d() == 0, "Dense recovery replayed old Begin events");
    restored.tick(2);
    require(restored.physics->contact_count_2d() == 0, "Dense recovered overlaps replayed");
    for (unsigned i = 4; i < 1024; ++i) {
        const auto entity = restored.scene.entity(source["entities"][i]["id"].get<std::string>());
        auto config = entity.get<PhysicsBody2D>();
        config.mass = 2;
        entity.set<PhysicsBody2D>(config);
    }
    restored.tick();
    require(restored.physics->contact_count_2d() == 8160,
            "Recovered dense replacement events were lost or duplicated");
    for (std::size_t i = 0; i < restored.physics->contact_count_2d(); ++i) {
        const auto event = *restored.physics->contact_event_2d(i);
        require(event.first_available && event.second_available == event.begin,
                "Restored hooks/epochs resolved through retired replacements");
    }
    Fixture second;
    second.recover(restored); // Native stale End buffers must not leak after recovery.
    second.tick();
    require(second.physics->contact_count_2d() == 0, "Recovery replayed a retired End buffer");
    for (unsigned i = 4; i < 1024; ++i)
        second.physics->teleport_2d(
            second.scene.reference(source["entities"][i]["id"].get<std::string>()), {0, 0}, 0,
            true);
    rejects([&] { second.tick(); }); // The restored native filter must still enforce pair work.
    rejects([&] { second.physics->adopt(); });
}
void native_remaining_budget() {
    Fixture test;
    test.load(test.source());
    auto config = PhysicsConfig{};
    config.gravity = {};
    const auto desired = physics_detail::desired_bodies_2d(test.engine.world(), config,
                                                           test.engine.world().transform_nodes());
    physics_detail::Native2D native(config);
    native.validate(desired);
    std::vector<std::uint64_t> discontinuities;
    native.synchronize(desired, desired, 0, {}, discontinuities);
    rejects([&] { (void)native.checkpoint(desired, 0); });
    const auto image = native.checkpoint(desired, 2 * 1024 * 1024);
    require(!image.bytes.empty(), "Native budget fixture produced no image");
    rejects([&] { (void)native.checkpoint(desired, image.bytes.size() - 1); });
    require(native.checkpoint(desired, image.bytes.size()).bytes.size() == image.bytes.size(),
            "Exact remaining native byte budget rejected");
    physics_detail::Native2D candidate(config);
    auto corrupt = image.bytes;
    corrupt[0] ^= 1;
    rejects([&] { candidate.restore(image.metadata, corrupt, desired, 0); });
    require(!candidate.realized(), "Failed native image published a candidate world");
    candidate.restore(image.metadata, image.bytes, desired, 0);
    require(candidate.size() == 1, "Failed native image prevented later valid recovery");
}
void parent_frame_recovery() {
    for (float depth : {-1.f, 0.f, 1.f}) {
        Fixture original;
        auto source = original.source(1);
        const auto parent = EntityId::generate().str();
        source["entities"][0]["spatial"] = {
            {"mode", "explicit"}, {"target", {{"scene", source["asset_id"]}, {"entity", parent}}}};
        source["entities"][0]["components"]["forge.local_translation"] = {
            {"x", 1}, {"y", 2}, {"z", 3}};
        source["entities"][0]["components"]["forge.local_scale"] = {{"x", -1}, {"y", 1}, {"z", -1}};
        source["entities"].push_back(
            {{"id", parent},
             {"name", "Signed parent"},
             {"spatial", {{"mode", "world"}}},
             {"components",
              {{"forge.local_translation", {{"x", 999980}, {"y", 2}, {"z", 10}}},
               {"forge.local_rotation",
                {{"x", 0}, {"y", 0}, {"z", std::sin(.1)}, {"w", std::cos(.1)}}},
               {"forge.local_scale", {{"x", -2}, {"y", 2}, {"z", depth}}}}}});
        original.load(source);
        const auto state = original.state();
        original.physics->move_kinematic_2d(original.ref(),
                                            {state.position[0] + .2, state.position[1]}, .1);
        original.tick();
        Fixture restored;
        restored.recover(original);
        for (unsigned i = 0; i < 10; ++i) {
            original.tick();
            restored.tick();
            equal_state(restored, original);
        }
        const auto actor = restored.scene.entity(original.actor);
        require(actor.get<LocalScale>() == LocalScale{-1, 1, -1} &&
                    actor.get<LocalTranslation>().z == 3,
                "Parent-frame recovery changed authored depth or scale channels");
    }
}
void controller_queue_recovery() {
    Fixture original;
    auto source = original.source();
    auto& components = source["entities"][0]["components"];
    components.erase("forge.physics_body_2d");
    components.erase("forge.box_collider_2d");
    components["forge.character_controller_2d"] = {{"mode", 1}};
    original.load(source);
    const auto ref = original.ref();
    original.physics->move_character_2d(ref, {-2, 1});
    original.physics->place_character_2d(ref, {999989, 3}, true);
    original.physics->move_character_2d(ref, {-1, 2});
    const auto checkpoint = original.physics->checkpoint();
    require(checkpoint.at("version") == 7 && checkpoint.at("character_commands").size() == 3,
            "2D character requests did not use the versioned shared private checkpoint");
    Fixture copy;
    copy.actor = original.actor;
    copy.scene.restore_snapshot(original.scene.snapshot());
    copy.physics->restore(checkpoint);
    copy.simulation.reset_presentation();
    for (unsigned step = 0; step < 3; ++step) {
        original.tick();
        copy.tick();
        const auto a = original.physics->character_2d(ref), b = copy.physics->character_2d(ref);
        const double dt = float(1.f / 60);
        require(a.position == b.position && a.velocity == b.velocity && a.epoch == b.epoch &&
                    a.ground == b.ground && a.supporting_entity == b.supporting_entity &&
                    a.placement_blocked == b.placement_blocked &&
                    std::abs(a.position[0] - (999989 - (step + 1) * dt)) < 1e-6 &&
                    std::abs(a.position[1] - (3 + (step + 1) * 2 * dt)) < 1e-6 &&
                    a.velocity == Double2{-1, 2} &&
                    original.physics->checkpoint().at("character_commands").empty() &&
                    copy.physics->checkpoint().at("character_commands").empty(),
                "2D character FIFO recovery diverged, reordered or replayed consumed requests");
    }
    for (unsigned corruption = 0; corruption < 9; ++corruption) {
        auto invalid = checkpoint;
        auto& command = invalid["character_commands"][0];
        switch (corruption) {
        case 0:
            invalid["version"] = 6;
            break;
        case 1:
            command["planar"] = false;
            break;
        case 2:
            command["kind"] = 1u;
            break;
        case 3:
            command["kind"] = 4294967296ull;
            break;
        case 4:
            command["kind"] = .5;
            break;
        case 5:
            command["velocity"] = {0, 0, 1};
            break;
        case 6:
            command["velocity"] = {0, 0, 0, 1};
            break;
        case 7:
            command["velocity"] = {1000001, 0, 0};
            break;
        case 8:
            invalid["character_commands"][1]["position"] = {0, 1000000, 0};
            break;
        }
        reseal(invalid);
        Fixture candidate;
        candidate.actor = original.actor;
        candidate.scene.restore_snapshot(source);
        const auto before = candidate.scene.snapshot();
        rejects([&] { candidate.physics->restore(invalid); });
        require(candidate.scene.snapshot() == before &&
                    !candidate.physics->status().at("native_2d_realized").get<bool>(),
                "Invalid2D controller recovery published native/source candidate state");
    }
}
} // namespace
int main() {
    try {
        roundtrips();
        queues_and_rejection();
        mixed_queue_and_empty_epoch();
        maximum_sensor_recovery();
        native_remaining_budget();
        parent_frame_recovery();
        controller_queue_recovery();
        std::cout << "2D character FIFO/version7/fresh-recovery/rejection PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "2D exact-build recovery, native bindings, continuation, queued controls and "
                 "rejection passed\n";
}
