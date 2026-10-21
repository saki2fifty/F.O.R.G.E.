#include <cmath>
#include <filesystem>
#include <forge/authoring.hpp>
#include <forge/prefab_authoring.hpp>
#include <forge/runtime.hpp>
#include <iostream>

using namespace forge;
namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
Json& row(Json& document, const std::string& id) {
    for (auto& entity : document["entities"])
        if (entity.at("id") == id)
            return entity;
    throw std::runtime_error("Missing connected-body source row");
}
std::string create(Scene& scene, const char* name) {
    return authoring_command(scene, "entity.create", {{"name", name}}).at("selected");
}
void property(Scene& scene, const std::string& id, const char* component, const char* field,
              Json value) {
    authoring_command(
        scene, "property.set",
        {{"entity", id}, {"component", component}, {"field", field}, {"value", std::move(value)}});
}
void add(Scene& scene, const std::string& id, const char* component) {
    authoring_command(scene, "component.add", {{"entity", id}, {"component", component}});
}
std::string mapped(Scene& scene, const std::string& instance, const std::string& member) {
    auto document = scene.document();
    return row(document, instance).at("prefab_instance").at("members").at(member);
}
void exercise(const std::filesystem::path& project) {
    std::filesystem::create_directories(project);
    EngineContext engine{WorldRole::Authoring};
    Scene scene(engine.world());
    const auto root = create(scene, "Connected assembly"), a = create(scene, "Anchor"),
               b = create(scene, "Bob"), j = create(scene, "Connection");
    for (const auto& id : {a, b, j})
        scene.reparent_entity(id, root, ReparentMode::KeepLocal);
    for (const auto& id : {a, b}) {
        add(scene, id, "forge.physics_body_2d");
        add(scene, id, "forge.box_collider_2d");
        property(scene, id, "forge.physics_body_2d", "mask", 0u);
    }
    property(scene, b, "forge.physics_body_2d", "motion", 2u);
    add(scene, j, "forge.physics_joint_2d");
    property(scene, j, "forge.physics_joint_2d", "body_a", scene.reference(a));
    property(scene, j, "forge.physics_joint_2d", "body_b", scene.reference(b));
    property(scene, j, "forge.physics_joint_2d", "distance", 2.);
    property(scene, j, "forge.physics_joint_2d", "collide_connected", false);
    const Json opaque{{"body_a", scene.reference(a)}, {"counter", UINT64_MAX}};
    auto document = scene.document();
    document["version"] = 5;
    for (const auto& id : {a, b}) {
        auto& entity = row(document, id);
        entity["spatial"] = {{"mode", "world"}};
        entity["components"]["forge.local_scale"] = {{"x", -1}, {"y", 1}, {"z", 0}};
    }
    row(document, a)["components"]["forge.local_translation"] = {{"x", 0}, {"y", 2}, {"z", 9}};
    row(document, b)["components"]["forge.local_translation"] = {{"x", 0}, {"y", 0}, {"z", 9}};
    row(document, j)["components"]["forge.physics_joint_2d"]["future"] = opaque;
    scene.edit(document);
    const auto before_copy = scene.document();
    const auto copy = scene.duplicate_subtree(root);
    auto copied = scene.document();
    std::string copied_a, copied_b, copied_j;
    for (const auto& entity : copied.at("entities"))
        if (entity.value("parent", std::string{}) == copy) {
            if (entity.at("name") == "Anchor")
                copied_a = entity.at("id");
            if (entity.at("name") == "Bob")
                copied_b = entity.at("id");
            if (entity.at("name") == "Connection")
                copied_j = entity.at("id");
        }
    require(!copied_a.empty() && !copied_b.empty() && !copied_j.empty(),
            "Subtree duplicate lost connected members");
    const auto copied_joint = scene.entity(copied_j).get<PhysicsJoint2D>();
    require(copied_joint.body_a == scene.reference(copied_a) &&
                copied_joint.body_b == scene.reference(copied_b) &&
                row(copied, copied_j).at("components").at("forge.physics_joint_2d").at("future") ==
                    opaque,
            "Subtree duplicate borrowed original bodies or rewrote opaque lookalikes");
    require(scene.undo() && scene.document() == before_copy && scene.redo() &&
                scene.document() == copied,
            "Connected subtree duplicate was not one reversible source operation");

    const auto definition = create_prefab_source(scene, root);
    require(definition.source.at("version") == 3,
            "Joint capture bypassed portable-reference format");
    std::string a_member, b_member, j_member;
    for (const auto& member : definition.source.at("members")) {
        if (member.at("name") == "Anchor")
            a_member = member.at("id");
        if (member.at("name") == "Bob")
            b_member = member.at("id");
        if (member.at("name") == "Connection") {
            j_member = member.at("id");
            const auto& value = member.at("components").at("forge.physics_joint_2d");
            require(value.at("body_a").contains("member") &&
                        value.at("body_b").contains("member") && value.at("future") == opaque,
                    "Prefab capture flattened typed links or interpreted opaque future data");
        }
    }
    require(!a_member.empty() && !b_member.empty() && !j_member.empty(),
            "Joint capture lost members");
    PrefabLibrary library(project);
    const auto asset = library.create(scene, definition, "Connected2D.prefab.json");
    const auto first = instantiate_prefab(scene, asset), second = instantiate_prefab(scene, asset);
    const auto first_j = mapped(scene, first, j_member), second_j = mapped(scene, second, j_member);
    auto check_links = [&](const std::string& instance) {
        const auto joint = scene.entity(mapped(scene, instance, j_member)).get<PhysicsJoint2D>();
        require(joint.body_a == scene.reference(mapped(scene, instance, a_member)) &&
                    joint.body_b == scene.reference(mapped(scene, instance, b_member)),
                "Prefab materialization borrowed another instance's body");
    };
    check_links(first);
    check_links(second);
    auto inherited = scene.document();
    require(!row(inherited, first_j).at("components").contains("forge.physics_joint_2d"),
            "Native instance references manufactured authored component ownership");
    property(scene, first_j, "forge.physics_joint_2d", "distance", 2.);
    auto intended = scene.document();
    require(row(intended, first_j)
                        .at("property_overrides")
                        .at("forge.physics_joint_2d")
                        .at("distance") == 2 &&
                !row(intended, first_j).at("components").contains("forge.physics_joint_2d"),
            "Equal joint value flattened explicit prefab override intent");
    require(scene.undo() && scene.document() == inherited && scene.redo() &&
                scene.document() == intended,
            "Joint override Undo/Redo lost inheritance");
    const auto instance_copy = scene.duplicate_subtree(first);
    check_links(instance_copy);
    auto expected = library.source(asset), changed = expected;
    changed["revision"] = 2u;
    for (auto& member : changed["members"])
        if (member.at("id") == j_member)
            member["components"]["forge.physics_joint_2d"]["distance"] = 3.;
    library.publish(scene, expected, changed);
    require(scene.entity(first_j).get<PhysicsJoint2D>().distance == 2 &&
                scene.entity(second_j).get<PhysicsJoint2D>().distance == 3,
            "Prefab publication ignored joint inheritance/explicit override boundaries");
    authoring_command(
        scene, "property.revert",
        {{"entity", first_j}, {"component", "forge.physics_joint_2d"}, {"field", "distance"}});
    require(scene.entity(first_j).get<PhysicsJoint2D>().distance == 3 && scene.undo() &&
                scene.entity(first_j).get<PhysicsJoint2D>().distance == 2 && scene.redo() &&
                scene.entity(first_j).get<PhysicsJoint2D>().distance == 3,
            "Joint Revert/history lost source intent");
    check_links(first);
    check_links(second);
    check_links(instance_copy);
    const auto saved = scene.snapshot();
    EngineContext restored_engine{WorldRole::Authoring};
    Scene restored(restored_engine.world());
    restored.restore_snapshot(saved);
    require(restored.document() == scene.document() &&
                restored.entity(first_j).get<PhysicsJoint2D>() ==
                    scene.entity(first_j).get<PhysicsJoint2D>(),
            "Joint source reopening changed instance identity or resolved native references");
    require(!engine.services().available(Capability::Physics), "Joint authoring created a solver");
    EngineContext runtime{WorldRole::Runtime, false, {physics_module()}};
    Scene played(runtime.world());
    played.restore_snapshot(saved);
    const auto physics = std::static_pointer_cast<PhysicsRuntime>(runtime.services().physics());
    PhysicsConfig config;
    config.gravity = {};
    physics->configure(config);
    physics->synchronize(0);
    require(physics->status().at("joints_2d") == 5,
            "Authored/duplicated/prefab joints did not realize");
    RuntimeSimulation simulation(runtime.world(), played);
    for (unsigned i = 0; i < 120; ++i)
        simulation.tick(1.f / 60);
    for (const auto& instance : {first, second, instance_copy}) {
        const auto source_joint =
            played.entity(mapped(scene, instance, j_member)).get<PhysicsJoint2D>();
        const auto a_state = physics->body_state_2d(source_joint.body_a);
        const auto b_state = physics->body_state_2d(source_joint.body_b);
        require(a_state && b_state &&
                    std::abs(std::hypot(a_state->position[0] - b_state->position[0],
                                        a_state->position[1] - b_state->position[1]) -
                             source_joint.distance) < .02,
                "Portable joint source did not drive its own runtime bodies");
    }
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected build-owned joint authoring scratch");
        const auto project = std::filesystem::path(argv[1]) / AssetId::generate().str();
        exercise(project);
        std::filesystem::remove_all(project);
        std::cout << "2D joint source history/subtree remapping/portable prefab capture and copies/"
                     "publication/Revert/opaque data/runtime realization passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
