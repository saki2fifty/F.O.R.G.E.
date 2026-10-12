#include "collision_authoring.hpp"
#include "collision_bundle.hpp"
#include "collision_document.hpp"
#include "editor/physics_joint_guide.hpp"
#include "physics_joint_value.hpp"
#include <chrono>
#include <cmath>
#include <forge/authoring.hpp>
#include <forge/runtime.hpp>
#include <forge/scene_identity.hpp>
#include <fstream>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <thread>
using namespace forge;
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F operation) {
    try {
        operation();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Expected rejection");
}
struct Fixture {
    EngineContext engine;
    explicit Fixture(std::filesystem::path root = {})
        : engine{WorldRole::Runtime, false, {physics_module({}, std::move(root))}} {}
    Scene scene{engine.world()};
    RuntimeSimulation simulation{engine.world(), scene};
    std::shared_ptr<PhysicsRuntime> physics =
        std::static_pointer_cast<PhysicsRuntime>(engine.services().physics());
    void tick(unsigned n = 1) {
        for (unsigned i = 0; i < n; ++i)
            simulation.tick(1.f / 60);
    }
    flecs::entity entity(const char* name) { return scene.entity(name); }
    EntityRef ref(const char* name) { return *engine.world().reference(entity(name)); }
    void setup() {
        scene.reset(
            {{"version", 1},
             {"entities",
              Json::array({{{"id", "a"},
                            {"name", "Anchor"},
                            {"components",
                             {{"forge.position", {{"x", 0}, {"y", 4}, {"z", 0}}},
                              {"forge.physics_body",
                               {{"motion", 0},
                                {"density", 1000},
                                {"mass", 0},
                                {"friction", .5},
                                {"restitution", 0},
                                {"gravity_factor", 1}}},
                              {"forge.box_collider", {{"x", .2}, {"y", .2}, {"z", .2}}}}}},
                           {{"id", "b"},
                            {"name", "Bob"},
                            {"components",
                             {{"forge.position", {{"x", 1.2}, {"y", 2.4}, {"z", 0}}},
                              {"forge.physics_body",
                               {{"motion", 2},
                                {"density", 1000},
                                {"mass", 1},
                                {"friction", .5},
                                {"restitution", 0},
                                {"gravity_factor", 1}}},
                              {"forge.box_collider", {{"x", .3}, {"y", .3}, {"z", .3}}}}}},
                           {{"id", "j"}, {"name", "Joint"}, {"components", Json::object()}},
                           {{"id", "j2"}, {"name", "Joint2"}, {"components", Json::object()}},
                           {{"id", "j3"}, {"name", "Joint3"}, {"components", Json::object()}}})}});
        PhysicsJoint joint;
        joint.body_a = ref("a");
        joint.body_b = ref("b");
        joint.min_distance = joint.max_distance = 2;
        entity("j").set<PhysicsJoint>(joint);
    }
};
Double3 point(Fixture& f, const char* name, Double3 local = {}) {
    f.engine.world().evaluate_world_transforms();
    return f.entity(name).get<WorldTransform>().affine.point(local);
}
double distance(Double3 a, Double3 b) {
    double total = 0;
    for (unsigned i = 0; i < 3; ++i)
        total += (a[i] - b[i]) * (a[i] - b[i]);
    return std::sqrt(total);
}
void simulate() {
    Fixture f;
    f.setup();
    const auto authored = f.scene.snapshot();
    EngineContext authoring;
    Scene authored_scene{authoring.world()};
    authored_scene.restore_snapshot(authored);
    f.tick();
    require(f.physics->status().at("joints") == 1, "Distance joint not realized");
    const auto first = point(f, "b");
    for (unsigned i = 0; i < 180; ++i) {
        f.tick();
        require(std::abs(distance(point(f, "a"), point(f, "b")) - 2) < .035, "Tether length drift");
    }
    require(distance(first, point(f, "b")) > .1, "Distance body did not swing");
    require(authored_scene.document() == authored, "Simulation changed authored document");
    auto joint = f.entity("j").get<PhysicsJoint>();
    joint.kind = 1;
    joint.anchor_b_y = 1;
    joint.min_angle = -45;
    joint.max_angle = 45;
    f.entity("j").set<PhysicsJoint>(joint);
    const float angle = 35 * std::numbers::pi_v<float> / 180;
    f.physics->teleport(f.ref("b"), {std::sin(angle), 4 - std::cos(angle), 0},
                        {0, 0, std::sin(angle / 2), std::cos(angle / 2)}, true);
    f.tick();
    auto start = point(f, "b");
    for (unsigned i = 0; i < 240; ++i) {
        f.tick();
        require(distance(point(f, "a"), point(f, "b", {0, 1, 0})) < .04, "Hinge pivot drift");
        const auto q = f.entity("b").get<LocalRotation>();
        require(std::abs(2 * std::atan2(q.z, q.w)) < .9, "Hinge escaped angular limits");
        require(std::abs(q.x) + std::abs(q.y) < .002, "Hinge rotated off axis");
    }
    require(distance(start, point(f, "b")) > .1, "Hinge body did not swing");
    // Start beyond each asymmetric limit so the solver must enforce both stops.
    joint.min_angle = -15;
    joint.max_angle = 20;
    f.entity("j").set<PhysicsJoint>(joint);
    f.tick();
    for (const float degrees : {-70.f, 70.f}) {
        const float radians = degrees * std::numbers::pi_v<float> / 180;
        f.physics->teleport(f.ref("b"), {std::sin(radians), 4 - std::cos(radians), 0},
                            {0, 0, std::sin(radians / 2), std::cos(radians / 2)}, true);
        f.tick(120);
        const auto q = f.entity("b").get<LocalRotation>();
        const float actual = 2 * std::atan2(q.z, q.w) * 180 / std::numbers::pi_v<float>;
        if (!(actual >= -17 && actual <= 22))
            throw std::runtime_error("Asymmetric hinge stop: start=" + std::to_string(degrees) +
                                     " actual=" + std::to_string(actual));
        require(distance(point(f, "a"), point(f, "b", {0, 1, 0})) < .04,
                "Angular stop lost hinge pivot");
    }
}
Double3 direction(Fixture& f, const char* name, Double3 axis) {
    const auto origin = point(f, name), end = point(f, name, axis);
    return {end[0] - origin[0], end[1] - origin[1], end[2] - origin[2]};
}
void point_and_fixed() {
    for (unsigned kind : {2u, 3u}) {
        Fixture f;
        f.setup();
        auto joint = f.entity("j").get<PhysicsJoint>();
        joint.kind = kind;
        joint.anchor_b_y = 1;
        f.entity("j").set<PhysicsJoint>(joint);
        auto support = f.entity("a").get<PhysicsBody>();
        support.mask = 0;
        f.entity("a").set<PhysicsBody>(support);
        const float angle = kind == 2 ? 40 * std::numbers::pi_v<float> / 180 : 0;
        const float diagonal = std::sqrt(.5f);
        f.entity("b").set<LocalTranslation>(
            {std::sin(angle) * diagonal, 4 - std::cos(angle), -std::sin(angle) * diagonal});
        f.entity("b").set<LocalRotation>({std::sin(angle / 2) * diagonal, 0,
                                          std::sin(angle / 2) * diagonal, std::cos(angle / 2)});
        const auto initial = point(f, "b");
        for (unsigned i = 0; i < 180; ++i) {
            f.tick();
            require(distance(point(f, "a"), point(f, "b", {0, 1, 0})) < .04,
                    "Point/fixed pivot drift");
            const auto q = f.entity("b").get<LocalRotation>();
            if (kind == 2 && i < 10)
                require(std::abs(q.x) > .1 && std::abs(q.z) > .1,
                        "Point joint incorrectly locked rotation axes");
            if (kind == 3)
                require(std::abs(q.x) + std::abs(q.y) + std::abs(q.z) < .002,
                        "Fixed joint allowed angular motion");
        }
        require(kind == 2 ? distance(initial, point(f, "b")) > .1
                          : distance(initial, point(f, "b")) < .01,
                "Point/fixed translational behavior differs");
        if (kind == 3) {
            // Nonmatching body-local frames require a known -90degree relative turn.
            joint.normal_b = 1;
            f.entity("j").set<PhysicsJoint>(joint);
            // Isolate frame construction from finite-iteration gravity-load error.
            auto moving = f.entity("b").get<PhysicsBody>();
            moving.gravity_factor = 0;
            f.entity("b").set<PhysicsBody>(moving);
            f.physics->teleport(f.ref("b"), {-1, 4, 0}, {0, 0, -diagonal, diagonal}, true);
            f.tick(120);
            const auto dx = distance(direction(f, "a", {1, 0, 0}), direction(f, "b", {0, 1, 0}));
            const auto dy = distance(direction(f, "a", {0, 1, 0}), direction(f, "b", {-1, 0, 0}));
            if (!(dx < .003 && dy < .003)) {
                const auto q = f.entity("b").get<LocalRotation>();
                throw std::runtime_error("Fixed frame error " + std::to_string(dx) + "," +
                                         std::to_string(dy) +
                                         " q=" + Json::array({q.x, q.y, q.z, q.w}).dump());
            }
            support.motion = 1;
            f.entity("a").set<PhysicsBody>(support);
            moving.gravity_factor = 1;
            f.entity("b").set<PhysicsBody>(moving);
            // Unit-vector chord for one degree, under the normal gravity/solver profile.
            const double loaded_frame_error = 2 * std::sin(std::numbers::pi_v<double> / 360);
            f.tick();
            // Move at 2.45 m/s over one second, rather than accelerating to
            // 147 m/s for one tick and exciting the offset body's solver modes.
            for (unsigned sample = 1; sample <= 60; ++sample) {
                const float t = float(sample) / 60;
                f.physics->move_kinematic(f.ref("a"), {t, 4 + t, 2 * t},
                                          {0, std::sin(.2f * t), 0, std::cos(.2f * t)});
                f.tick();
                require(distance(point(f, "a"), point(f, "b", {0, 1, 0})) < .04,
                        "Fixed attachment lost pivot while support moved");
            }
            f.tick(120);
            require(distance(point(f, "a"), {1, 5, 2}) < .01 &&
                        distance(point(f, "a"), point(f, "b", {0, 1, 0})) < .04 &&
                        distance(direction(f, "a", {1, 0, 0}), direction(f, "b", {0, 1, 0})) <
                            loaded_frame_error,
                    "Fixed body failed to follow moving/rotating kinematic support");
        }
    }
}
void four_type_recovery() {
    Fixture f;
    f.setup();
    auto support = f.entity("a").get<PhysicsBody>();
    support.mask = 0;
    f.entity("a").set<PhysicsBody>(support);
    f.entity("b").set<LocalTranslation>({0, 3, 0});
    auto joint = f.entity("j").get<PhysicsJoint>();
    joint.min_distance = joint.max_distance = 1;
    f.entity("j").set<PhysicsJoint>(joint);
    for (const auto [name, kind] : {std::pair{"j2", 1u}, {"j3", 2u}}) {
        auto next = joint;
        next.kind = kind;
        next.anchor_b_y = 1;
        f.entity(name).set<PhysicsJoint>(next);
    }
    const auto add = [&](const char* name, unsigned kind) {
        const auto id = authoring_command(f.scene, "entity.create", {{"name", name}})
                            .at("selected")
                            .get<std::string>();
        auto next = joint;
        next.kind = kind;
        next.anchor_b_y = kind ? 1 : 0;
        f.scene.entity(id).set<PhysicsJoint>(next);
        return id;
    };
    const auto fixed = add("Fixed", 3);
    add("Slider", 4);
    add("Swing Twist", 5);
    const auto distance_id = add("Last distance", 0);
    f.physics->teleport(f.ref("b"), {.2, 3, .1}, {0, 0, .1f, std::sqrt(.99f)}, true);
    f.tick(3);
    require(f.physics->status().at("joints") == 7, "Six native joint types not realized");
    joint.enabled = false;
    f.entity("j").set<PhysicsJoint>(joint);
    f.tick();
    const auto snapshot = f.scene.snapshot(), checkpoint = f.physics->checkpoint();
    require(checkpoint.at("joints").size() == 6 &&
                checkpoint.at("joints").at(0).at("entity").at("entity") == distance_id,
            "Six-type native swapped-order capture differs");
    Fixture recovered;
    recovered.scene.restore_snapshot(snapshot);
    recovered.physics->restore(checkpoint);
    require(recovered.physics->checkpoint().at("joints") == checkpoint.at("joints"),
            "Six-type native mapping/configuration not restored");
    for (unsigned i = 0; i < 45; ++i) {
        f.tick();
        recovered.tick();
        require(distance(point(f, "b"), point(recovered, "b")) < 1e-5 &&
                    distance(direction(f, "b", {1, 0, 0}), direction(recovered, "b", {1, 0, 0})) <
                        1e-5,
                "Six-type restored trajectory/frame diverged");
    }
    auto invalid = recovered.scene.entity(fixed).get<PhysicsJoint>();
    invalid.normal_a = invalid.axis_a;
    recovered.scene.entity(fixed).set<PhysicsJoint>(invalid);
    const auto before = recovered.physics->status();
    rejects([&] { recovered.physics->synchronize(1.f / 60); });
    require(recovered.physics->status() == before, "Invalid fixed frame mutated native world");
    invalid = f.scene.entity(fixed).get<PhysicsJoint>();
    invalid.kind = 6;
    recovered.scene.entity(fixed).set<PhysicsJoint>(invalid);
    rejects([&] { recovered.physics->synchronize(1.f / 60); });
    require(recovered.physics->status() == before, "Unknown joint kind mutated native world");
}
void lifecycle_and_recovery() {
    Fixture f;
    f.setup();
    auto joint = f.entity("j").get<PhysicsJoint>();
    joint.min_distance = 1;
    f.entity("j").set<PhysicsJoint>(joint);
    auto hinge = joint;
    hinge.kind = 1;
    hinge.anchor_b_y = 1;
    f.entity("j2").set<PhysicsJoint>(hinge);
    f.entity("j3").set<PhysicsJoint>(joint);
    f.tick(5);
    require(f.physics->status().at("joints") == 3, "Multiple joints not realized");
    joint.enabled = false;
    f.entity("j").set<PhysicsJoint>(joint);
    f.tick();
    auto snapshot = f.scene.snapshot();
    auto checkpoint = f.physics->checkpoint();
    require(checkpoint.at("version") == 4, "Joint recovery envelope version");
    require(checkpoint.at("joints").at(0).at("entity") == Json(f.ref("j3")),
            "Native swapped index order was lost");
    Fixture restored;
    restored.scene.restore_snapshot(snapshot);
    restored.physics->restore(checkpoint);
    require(restored.physics->status().at("joints") == 2, "Restored joints missing");
    require(restored.physics->checkpoint().at("joints") == checkpoint.at("joints"),
            "Restored native joint order/configuration differs");
    for (unsigned i = 0; i < 45; ++i) {
        f.tick();
        restored.tick();
        require(distance(point(f, "b"), point(restored, "b")) < 1e-5,
                "Restored joint motion diverged");
    }
    auto bad = f.entity("j2").get<PhysicsJoint>();
    bad.max_distance = 2.5;
    restored.entity("j2").set<PhysicsJoint>(bad);
    rejects([&] { restored.physics->checkpoint(); });
    Fixture mismatch;
    mismatch.scene.restore_snapshot(snapshot);
    mismatch.entity("j2").set<PhysicsJoint>(bad);
    rejects([&] { mismatch.physics->restore(checkpoint); });
    require(mismatch.physics->status().at("bodies") == 0 &&
                mismatch.physics->status().at("joints") == 0,
            "Invalid recovery mutated native world");
    auto body = f.entity("b").get<PhysicsBody>();
    body.mass = 2;
    f.entity("b").set<PhysicsBody>(body);
    f.tick();
    require(f.physics->status().at("joints") == 2, "Body replacement dropped joints");
    f.entity("j2").remove<PhysicsJoint>();
    f.entity("j3").remove<PhysicsJoint>();
    f.entity("b").destruct();
    f.tick();
    require(f.physics->status().at("joints") == 0 && f.physics->status().at("bodies") == 1,
            "Body retirement retained constraints");
    f.physics->stop();
    f.physics->stop();
}
void rejection_and_scale() {
    Fixture f;
    f.setup();
    f.tick();
    const auto status = f.physics->status();
    auto original = f.entity("j").get<PhysicsJoint>();
    for (unsigned test = 0; test < 7; ++test) {
        auto v = original;
        if (test == 0)
            v.body_b = v.body_a;
        if (test == 1)
            v.body_b = {};
        if (test == 2)
            v.body_b.entity = EntityId::generate();
        if (test == 3) {
            v.kind = 1;
            v.normal_a = v.axis_a;
        }
        if (test == 4)
            v.max_distance = 1;
        if (test == 5)
            v.min_distance = std::numeric_limits<float>::infinity();
        if (test == 6) {
            v.kind = 1;
            v.min_angle = v.max_angle = 0;
        }
        f.entity("j").set<PhysicsJoint>(v);
        rejects([&] { f.physics->synchronize(1.f / 60); });
        require(f.physics->status() == status, "Rejected joint altered native state");
        require(!f.engine.services().diagnostics().empty(), "Joint rejection lacks diagnostic");
    }
    f.entity("j").set<PhysicsJoint>(original);
    auto a = f.entity("a").get<PhysicsBody>();
    auto b = f.entity("b").get<PhysicsBody>();
    b.sensor = true;
    f.entity("b").set<PhysicsBody>(b);
    rejects([&] { f.physics->synchronize(1.f / 60); });
    b.sensor = false;
    b.motion = 0;
    f.entity("b").set<PhysicsBody>(b);
    rejects([&] { f.physics->synchronize(1.f / 60); });
    b.motion = 2;
    f.entity("b").set<PhysicsBody>(b);
    a.motion = 2;
    f.entity("a").set<PhysicsBody>(a);
    f.tick(80);
    require(point(f, "a")[1] < 3, "Dynamic pair did not fall");
    require(std::abs(distance(point(f, "a"), point(f, "b")) - 2) < .04,
            "Dynamic pair separation drift");
    Fixture scaled;
    scaled.setup();
    scaled.entity("a").set<LocalScale>({-2, 1, 1});
    auto v = scaled.entity("j").get<PhysicsJoint>();
    v.anchor_a_x = .5f;
    scaled.entity("j").set<PhysicsJoint>(v);
    for (unsigned i = 0; i < 180; ++i) {
        scaled.tick();
        require(i < 40 || std::abs(distance(point(scaled, "a", {.5, 0, 0}), point(scaled, "b")) -
                                   2) < .04,
                "Signed scale anchor conversion");
    }
}
void offset_center_of_mass(const std::filesystem::path& scratch) {
    const auto root = std::filesystem::absolute(scratch) / AssetId::generate().str();
    std::filesystem::create_directories(root / "Assets");
    {
        auto lease = std::make_shared<ProjectLease>(root);
        auto source = CollisionSource::create(AssetId::generate(), CollisionKind::Box);
        source.document["nodes"][0]["translation"] = {.7, 0., 0.};
        source.document["nodes"][0]["dimensions"] = {.3, .3, .3};
        source.validate();
        {
            std::ofstream out(root / "Assets/test.collision.json");
            out << source.document.dump();
            require(bool(out.flush()), "Offset source write failed");
        }
        AssetImportService service(lease, collision_import_registry(), collision_import_target());
        service.submit(
            service.prepare("Assets/test.collision.json", {}, source.asset()),
            [](auto& candidate, const auto& plan, const auto&) {
                prepare_collision_publication(candidate, plan);
            },
            [](const auto&, const auto& artifact) {
                (void)collision_detail::decode_bundle(artifact.files);
            });
        require(service.wait_idle(std::chrono::seconds(15)), "Offset collider import timeout");
        const auto result = service.poll();
        require(result.size() == 1 && result[0].published, "Offset collider publication failed");
        Fixture f(root);
        f.setup();
        f.entity("b").remove<BoxCollider>().set<AssetCollider>({{source.asset()}});
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!f.physics->prepare_assets()) {
            require(std::chrono::steady_clock::now() < deadline,
                    "Offset native collider preparation timeout");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        for (unsigned i = 0; i < 180; ++i) {
            f.tick();
            require(std::abs(distance(point(f, "a"), point(f, "b")) - 2) < .04,
                    "Joint anchor used collider COM instead of entity origin");
        }
        for (unsigned kind : {1u, 2u, 3u, 5u}) {
            auto joint = f.entity("j").get<PhysicsJoint>();
            joint.kind = kind;
            joint.anchor_b_x = .3f;
            joint.anchor_b_y = .5f;
            joint.anchor_b_z = -.2f;
            f.entity("j").set<PhysicsJoint>(joint);
            f.entity("b").set<LocalScale>({-1, 2, .75f});
            f.physics->teleport(f.ref("b"), {.3, 3, .15}, {}, true);
            for (unsigned tick = 0; tick < 120; ++tick) {
                if (tick % 20 == 0)
                    f.physics->add_force(f.ref("b"), {0, 0, .1});
                f.tick();
                const auto anchor = point(f, "b", {.3f, .5f, -.2f});
                require(distance(point(f, "a"), anchor) < .04,
                        "Hinge/point/fixed scaled anchor confused body origin and offset COM");
                if (tick % 20 == 0) {
                    const auto guide = ui::prepare_physics_joint_guide(f.scene.effective_document(),
                                                                       f.scene.canonical_id("j"));
                    require(guide && guide->valid &&
                                distance(guide->bodies[0].anchor, point(f, "a")) < 1e-5 &&
                                distance(guide->bodies[1].anchor, anchor) < 1e-5,
                            "Scene guide differs from actual solver body-origin anchor");
                }
            }
        }
        {
            auto joint = f.entity("j").get<PhysicsJoint>();
            joint.kind = 4;
            joint.axis_a = joint.axis_b = 0;
            joint.normal_a = joint.normal_b = 1;
            joint.min_travel = -.75f;
            joint.max_travel = 1.25f;
            f.entity("j").set<PhysicsJoint>(joint);
            const auto original_body = f.entity("b").get<PhysicsBody>();
            auto moving = original_body;
            moving.gravity_factor = moving.linear_damping = moving.angular_damping = 0;
            moving.allow_sleeping = false;
            f.entity("b").set<PhysicsBody>(moving);
            for (int sign : {-1, 1}) {
                f.physics->teleport(f.ref("b"), {.3, 3, .15}, {}, true);
                // The tiny shape has an anchor far from COM. Separate conversion
                // from finite-iteration impact rebound by loading the stop steadily.
                f.physics->set_linear_velocity(f.ref("b"), {sign * 1., 0, 0});
                for (unsigned tick = 0; tick < 360; ++tick) {
                    f.physics->add_force(f.ref("b"), {sign * .5, 0, 0});
                    f.tick();
                    const auto state = f.physics->body_state(f.ref("b"));
                    require(state &&
                                distance(state->centre_of_mass, point(f, "b", {.7, 0, 0})) < 1e-5,
                            "Slider cooked signed COM no longer matches its actual body pose");
                }
                auto expected = point(f, "a");
                expected[0] += sign < 0 ? joint.min_travel : joint.max_travel;
                require(distance(point(f, "b", {.3f, .5f, -.2f}), expected) < .004,
                        "Slider signed cooked COM conversion changed its linear endpoint");
                const auto state = f.physics->body_state(f.ref("b"));
                require(state && distance(state->linear_velocity, {}) < .001,
                        "Slider cooked COM endpoint failed to settle under steady load");
            }
            f.entity("b").set<PhysicsBody>(original_body);
            f.tick();
        }
        // Pinned ScaledShape retains signed scale in GetInnerRadius; CCD must
        // reject this opt-in before retiring the currently valid joint/body.
        auto body = f.entity("b").get<PhysicsBody>();
        body.continuous_collision = true;
        f.entity("b").set<PhysicsBody>(body);
        const auto before = f.physics->status();
        bool rejected = false;
        try {
            f.physics->synchronize(1.f / 60);
        } catch (const std::exception& e) {
            rejected =
                std::string(e.what()).find("positive native inner radius") != std::string::npos;
        }
        require(rejected && f.physics->status() == before,
                "Invalid CCD radius was not rejected before body/joint mutation");
        body.continuous_collision = false;
        f.entity("b").set<PhysicsBody>(body);
        f.tick();
        require(f.physics->status().at("joints") == 1,
                "Default discrete mode failed to preserve mirrored collision asset");
        // Exercise real prepared signed geometry and its offset center of mass.
        // The impulse at COM must translate without spinning; body origin is
        // .7m to the right of COM after the negative-X scale, so it adds +Z torque.
        f.entity("j").remove<PhysicsJoint>();
        body.gravity_factor = 0;
        body.mass = 2;
        f.entity("b").set<PhysicsBody>(body);
        for (unsigned application = 0; application < 3; ++application) {
            f.physics->teleport(f.ref("b"), {.3, 3, .15}, {}, true);
            f.tick();
            const auto initial_com = point(f, "b", {.7, 0, 0});
            if (application == 0)
                f.physics->add_impulse(f.ref("b"), {0, .3, 0});
            else if (application == 1)
                f.physics->add_impulse(
                    f.ref("b"), {0, .3, 0},
                    LocalTranslation{initial_com[0], initial_com[1], initial_com[2]});
            else
                f.physics->add_impulse(f.ref("b"), {0, .3, 0}, LocalTranslation{.3, 3, .15});
            f.tick();
            const auto com = point(f, "b", {.7, 0, 0});
            const auto q = f.entity("b").get<LocalRotation>();
            const double dt = double(1.f / 60), damping = 1 - .05 * dt;
            require(std::abs(com[1] - initial_com[1] - .3 / 2 * damping * dt) < 1e-6 &&
                        std::abs(com[0] - initial_com[0]) < 1e-6 &&
                        std::abs(com[2] - initial_com[2]) < 1e-6,
                    "Offset signed collider impulse failed actual COM translation");
            const double inertia = 2. / 12 * (.3 * .3 + .6 * .6);
            const double angle = application == 2 ? .7 * .3 / inertia * damping * dt : 0;
            require(std::abs(q.z - std::sin(angle * .5)) < 1e-6 &&
                        std::abs(q.w - std::cos(angle * .5)) < 1e-6,
                    "World point impulse used body origin in place of offset scaled COM/inertia");
            const auto observed = f.physics->body_state(f.ref("b"));
            const double angular = application == 2 ? .7 * .3 / inertia * damping : 0;
            require(observed && observed->motion == 2 && observed->active &&
                        distance(observed->centre_of_mass, com) < 1e-6 &&
                        std::abs(observed->linear_velocity[1] - .3 / 2 * damping) < 1e-6 &&
                        std::abs(observed->angular_velocity[2] - angular) < 1e-6,
                    "Signed cooked body observation confused native COM/inertia with origin");
            const auto velocity =
                f.physics->point_velocity(f.ref("b"), {com[0] + .2, com[1] + .4, com[2] - .1});
            require(velocity && std::abs((*velocity)[0] + angular * .4) < 1e-6 &&
                        std::abs((*velocity)[1] - .3 / 2 * damping - angular * .2) < 1e-6 &&
                        std::abs((*velocity)[2]) < 1e-6,
                    "Signed cooked point velocity failed angular cross actual COM equation");
        }
        body.linear_damping = body.angular_damping = 0;
        f.entity("b").set<PhysicsBody>(body);
        f.physics->teleport(f.ref("b"), {.3, 3, .15}, {}, true);
        f.tick();
        const auto initial = f.physics->body_state(f.ref("b"));
        f.physics->set_linear_velocity(f.ref("b"), {0, 1, 0});
        f.physics->set_angular_velocity(f.ref("b"), {0, 0, 1});
        f.tick();
        const auto state = f.physics->body_state(f.ref("b"));
        const double dt = double(1.f / 60);
        require(initial && state && state->linear_velocity == Double3{0, 1, 0} &&
                    state->angular_velocity == Double3{0, 0, 1} &&
                    std::abs(state->centre_of_mass[1] - initial->centre_of_mass[1] - dt) < 1e-6,
                "Signed cooked velocity setter failed actual center-of-mass motion");
        const auto origin = point(f, "b");
        const auto velocity =
            f.physics->point_velocity(f.ref("b"), {origin[0], origin[1], origin[2]});
        require(velocity &&
                    std::abs((*velocity)[0] + origin[1] - state->centre_of_mass[1]) < 1e-6 &&
                    std::abs((*velocity)[1] - 1 - origin[0] + state->centre_of_mass[0]) < 1e-6,
                "Signed cooked velocity setter confused body origin with actual native COM");
    }
    require(std::filesystem::remove_all(root) > 0, "Offset fixture cleanup failed");
}
void capacity_and_transform_admission() {
    Fixture f;
    f.setup();
    auto doc = f.scene.document();
    auto joint = doc.at("entities").at(2);
    for (unsigned i = 1; i < 4096; ++i) {
        joint["id"] = EntityId::generate();
        joint["name"] = "Joint " + std::to_string(i);
        doc["entities"].push_back(joint);
    }
    f.scene.restore_snapshot(doc);
    f.physics->synchronize(0);
    require(f.physics->status().at("joints") == 4096, "Maximum joint profile not realized");
    const auto checkpoint = f.physics->checkpoint();
    require(checkpoint.at("joints").size() == 4096, "Maximum profile recovery mapping missing");
    f.engine.world().world().entity().set<PhysicsJoint>(f.entity("j").get<PhysicsJoint>());
    const auto before = f.physics->status();
    rejects([&] { f.physics->synchronize(1.f / 60); });
    require(f.physics->status() == before, "Capacity rejection mutated native constraints");
    Fixture bounds;
    bounds.setup();
    auto v = bounds.entity("j").get<PhysicsJoint>();
    v.anchor_a_x = 10000;
    bounds.entity("j").set<PhysicsJoint>(v);
    auto nodes = bounds.engine.world().transform_nodes();
    nodes.at(bounds.entity("a").id()).local.scale.x = 2;
    rejects([&] { bounds.physics->validate_transform_candidate(nodes); });
    require(bounds.physics->status().at("bodies") == 0,
            "Rejected animation anchor candidate realized bodies");
}
void prefab_values() {
    Fixture f;
    f.setup();
    const auto member = PrefabMemberId::generate();
    const auto asset = AssetId::generate();
    const PrefabDocument prefab(
        {{"format", "forge.prefab"},
         {"version", 1},
         {"asset_id", asset},
         {"revision", 1},
         {"root", member},
         {"members",
          Json::array({{{"id", member},
                        {"name", "Inherited joint"},
                        {"components",
                         {{"forge.physics_joint",
                           physics_detail::joint_value(f.entity("j").get<PhysicsJoint>())}}}}})}});
    f.scene.set_prefab_sources({{asset, prefab.source}});
    const auto create = [&] {
        return authoring_command(f.scene, "prefab.instantiate", {{"asset", asset}})
            .at("selected")
            .get<std::string>();
    };
    const auto one = create(), two = create();
    require(!f.scene.entity(one).owns<PhysicsJoint>() &&
                f.scene.entity(one).get<PhysicsJoint>().body_b == f.ref("b"),
            "Prefab joint did not retain typed inherited references");
    authoring_command(f.scene, "property.set",
                      {{"entity", one},
                       {"component", "forge.physics_joint"},
                       {"field", "max_distance"},
                       {"value", 3}});
    require(f.scene.entity(one).owns<PhysicsJoint>() &&
                f.scene.entity(two).get<PhysicsJoint>().max_distance == 2,
            "Joint instance edit changed sibling/source");
    require(f.scene.undo() && !f.scene.entity(one).owns<PhysicsJoint>(),
            "Joint instance Undo did not restore inheritance");
    auto source = prefab.source;
    source["revision"] = 2;
    source["members"][0]["components"]["forge.physics_joint"]["max_distance"] = 2.5;
    f.scene.publish_prefab_sources({{asset, source}}, [] {});
    require(f.scene.entity(one).get<PhysicsJoint>().max_distance == 2.5 &&
                f.scene.entity(two).get<PhysicsJoint>().max_distance == 2.5,
            "Joint source publication did not update inherited values");
    Fixture reopened;
    reopened.scene.restore_snapshot(f.scene.snapshot());
    require(!reopened.scene.entity(one).owns<PhysicsJoint>() &&
                reopened.scene.entity(one).get<PhysicsJoint>() ==
                    f.scene.entity(one).get<PhysicsJoint>(),
            "Joint prefab snapshot changed inheritance/references");
    reopened.tick();
    require(reopened.physics->status().at("joints") == 3,
            "Inherited joint runtime consumer missing");
}
void authoring_and_identity() {
    Fixture f;
    f.setup();
    const Json opaque = f.ref("a");
    auto extended = f.scene.document();
    for (auto& row : extended["entities"])
        if (row["id"] == f.scene.canonical_id("j"))
            row["components"]["forge.physics_joint"]["opaque"] = opaque;
    f.scene.restore_snapshot(extended);
    auto snapshot = f.scene.snapshot();
    const auto id = f.scene.canonical_id("j");
    authoring_command(f.scene, "property.set",
                      {{"entity", id},
                       {"component", "forge.physics_joint"},
                       {"field", "max_distance"},
                       {"value", 3}});
    require(f.entity("j").get<PhysicsJoint>().max_distance == 3, "Reflected joint edit");
    require(f.scene.undo() && f.entity("j").get<PhysicsJoint>().max_distance == 2, "Joint undo");
    require(f.scene.redo() && f.entity("j").get<PhysicsJoint>().max_distance == 3, "Joint redo");
    f.scene.undo();
    auto copy = duplicate_scene_asset(f.scene.snapshot());
    Fixture duplicated;
    duplicated.scene.restore_snapshot(copy);
    auto id2 = resolve_legacy_id(copy, "j");
    auto v = duplicated.scene.entity(id2).get<PhysicsJoint>();
    require(v.body_a.scene == duplicated.scene.asset_id() &&
                v.body_b.scene == duplicated.scene.asset_id(),
            "Duplicated builtin refs retained old scene");
    require(v.body_a.entity != f.ref("a").entity && v.body_b.entity != f.ref("b").entity,
            "Duplicated builtin body IDs not remapped");
    const auto copied = duplicated.scene.document();
    for (const auto& row : copied.at("entities"))
        if (row.at("id") == id2)
            require(row.at("components").at("forge.physics_joint").at("opaque") == opaque,
                    "Opaque extension was scanned/remapped");
    duplicated.tick();
    require(duplicated.physics->status().at("joints") == 1, "Duplicated joint did not resolve");
    const auto group = authoring_command(f.scene, "entity.create", {{"name", "Rig"}})
                           .at("selected")
                           .get<std::string>();
    for (const char* name : {"a", "b", "j"})
        f.entity(name).child_of(f.scene.entity(group));
    const auto clone = authoring_command(f.scene, "entity.duplicate", {{"entity", group}})
                           .at("selected")
                           .get<std::string>();
    bool cloned_joint = false;
    const auto cloned = f.scene.document();
    for (const auto& row : cloned.at("entities"))
        if (row.value("parent", std::string{}) == clone &&
            row.at("components").contains("forge.physics_joint")) {
            const auto value = f.scene.entity(row.at("id")).get<PhysicsJoint>();
            require(value.body_a.scene == f.scene.asset_id() &&
                        value.body_a.entity != f.ref("a").entity &&
                        value.body_b.entity != f.ref("b").entity,
                    "Entity subtree clone did not remap builtin joint references");
            cloned_joint = true;
        }
    require(cloned_joint, "Subtree clone omitted joint");
    auto schema = f.scene.schema();
    for (const auto& c : schema.at("components"))
        if (c.at("id") == "forge.physics_joint") {
            bool a = false, b = false;
            for (const auto& field : c.at("fields")) {
                if (field.at("id") == "body_a")
                    a = field.at("type") == "entity_ref";
                if (field.at("id") == "body_b")
                    b = field.at("type") == "entity_ref";
            }
            require(a && b, "Joint refs lost picker metadata");
            return;
        }
    throw std::runtime_error("Joint absent from schema");
}
#include "physics_distance_spring_tests.hpp"
#include "physics_joint_collision_tests.hpp"
#include "physics_motor_tests.hpp"
#include "physics_position_motor_tests.hpp"
#include "physics_slider_tests.hpp"
#include "physics_swing_twist_tests.hpp"
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2 || (argc == 3 && std::string_view(argv[2]) == "springs"),
                "Need joint scratch root and optional springs gate");
        if (argc == 3) {
            distance_springs();
            std::cout
                << "Distance springs, independent integration/reaction and fresh recovery passed\n";
            return 0;
        }
        simulate();
        point_and_fixed();
        slider_motion_and_recovery();
        joint_velocity_motors();
        joint_position_motors();
        joint_collision_policy();
        swing_twist_limits_and_recovery();
        four_type_recovery();
        lifecycle_and_recovery();
        rejection_and_scale();
        authoring_and_identity();
        prefab_values();
        offset_center_of_mass(argv[1]);
        capacity_and_transform_admission();
        std::cout << "Distance/hinge simulation, body lifetime, recovery order, rejection, scale, "
                     "history and identity passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
