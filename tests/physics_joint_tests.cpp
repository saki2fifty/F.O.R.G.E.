#include "collision_authoring.hpp"
#include "collision_bundle.hpp"
#include "collision_document.hpp"
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
    require(checkpoint.at("version") == 2, "Joint recovery envelope version");
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
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Need joint scratch root");
        simulate();
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
