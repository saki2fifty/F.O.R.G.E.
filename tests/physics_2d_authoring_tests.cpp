#include <filesystem>
#include <forge/authoring.hpp>
#include <forge/physics.hpp>
#include <forge/prefab_authoring.hpp>
#include <forge/runtime.hpp>
#include <iostream>
#include <string_view>

using namespace forge;
namespace {
void require(bool value, const char* text) {
    if (!value)
        throw std::runtime_error(text);
}
template <class F> void rejects(F&& operation) {
    try {
        operation();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid authored 2D value accepted");
}
void command(Scene& scene, const char* op, const std::string& id, const char* component,
             const char* field = nullptr, Json value = nullptr) {
    Json args{{"entity", id}, {"component", component}};
    if (field) {
        args["field"] = field;
        if (std::string_view(op) == "property.set")
            args["value"] = std::move(value);
    }
    (void)authoring_command(scene, op, args);
}
const Json& row(const Json& scene, const std::string& id) {
    for (const auto& item : scene.at("entities"))
        if (item.at("id") == id)
            return item;
    throw std::runtime_error("Missing authored entity row");
}
struct Cleanup {
    std::filesystem::path path;
    ~Cleanup() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};
void standalone_commands(const std::filesystem::path& base) {
    EngineContext engine{WorldRole::Authoring, false, {physics_schema_module()}};
    Scene scene(engine.world());
    const auto id = EntityId::generate().str();
    scene.reset({{"version", 5},
                 {"asset_id", AssetId::generate()},
                 {"entities",
                  Json::array({{{"id", id},
                                {"name", "Authored planar body"},
                                {"spatial", {{"mode", "world"}}},
                                {"components",
                                 {{"forge.local_translation", {{"x", 0}, {"y", 3}, {"z", 7}}},
                                  {"forge.local_scale", {{"x", -1}, {"y", 1}, {"z", 0}}}}}}})}});
    const auto before = scene.document();
    command(scene, "component.add", id, "forge.physics_body_2d");
    require(scene.entity(id).get<PhysicsBody2D>() == PhysicsBody2D{}, "Body2D add lost defaults");
    require(scene.undo() && scene.document() == before, "Body2D add undo lost source");
    require(scene.redo() && scene.entity(id).has<PhysicsBody2D>(), "Body2D add redo lost data");
    command(scene, "component.add", id, "forge.box_collider_2d");
    command(scene, "property.set", id, "forge.physics_body_2d", "motion", 2);
    command(scene, "property.set", id, "forge.physics_body_2d", "density", 2.5);
    command(scene, "property.set", id, "forge.physics_body_2d", "mass", 2);
    command(scene, "property.set", id, "forge.physics_body_2d", "angular_damping", .25);
    command(scene, "property.set", id, "forge.box_collider_2d", "x", 2);
    const auto saved = scene.document();
    const auto revision = scene.revision();
    rejects([&] { command(scene, "property.set", id, "forge.physics_body_2d", "density", 0); });
    require(scene.document() == saved && scene.revision() == revision,
            "Rejected Body2D property changed source/history");
    require(!engine.services().available(Capability::Physics), "Authoring created a solver");
    Cleanup work{base / AssetId::generate().str()};
    std::filesystem::create_directories(work.path);
    scene.save(work.path / "authored.scene.json");
    EngineContext reopened_engine{WorldRole::Authoring, false, {physics_schema_module()}};
    Scene reopened(reopened_engine.world());
    reopened.load(work.path / "authored.scene.json");
    require(reopened.document() == saved &&
                reopened.entity(id).get<PhysicsBody2D>() == scene.entity(id).get<PhysicsBody2D>(),
            "Body2D save/reopen lost source or native data");
    EngineContext runtime{WorldRole::Runtime, false, {physics_module()}};
    Scene played(runtime.world());
    played.restore_snapshot(reopened.snapshot());
    auto physics = std::static_pointer_cast<PhysicsRuntime>(runtime.services().physics());
    physics->synchronize(0);
    RuntimeSimulation simulation(runtime.world(), played);
    simulation.tick(1.f / 60);
    const auto state = physics->body_state_2d(played.reference(id));
    require(state && state->mass == 2 && state->position[1] < 3 &&
                played.entity(id).get<LocalTranslation>().z == 7 &&
                played.entity(id).get<LocalScale>() == LocalScale{-1, 1, 0},
            "Authored command/save path did not realize the same planar body");
}
void prefab_intent() {
    EngineContext engine{WorldRole::Authoring, false, {physics_schema_module()}};
    Scene scene(engine.world());
    const auto member = PrefabMemberId::generate();
    const auto asset = AssetId::generate();
    const Json opaque{{"large", UINT64_MAX}, {"future", Json::array({1, "keep", nullptr})}};
    PrefabDocument prefab(
        {{"format", "forge.prefab"},
         {"version", 2},
         {"asset_id", asset},
         {"revision", 1u},
         {"root", member},
         {"members", Json::array({{{"id", member},
                                   {"name", "Planar prefab"},
                                   {"components",
                                    {{"forge.local_scale", {{"x", -1}, {"y", 1}, {"z", 0}}},
                                     {"forge.physics_body_2d",
                                      {{"motion", 2}, {"mass", 2}, {"future_options", opaque}}},
                                     {"forge.circle_collider_2d", {{"radius", .5}}}}}}})}});
    scene.publish_prefab_sources({{asset, prefab.source}}, [] {});
    const auto id = EntityId::generate().str();
    auto document = scene.document();
    document["version"] = 5;
    document["entities"].push_back(
        {{"id", id},
         {"name", "Instance"},
         {"spatial", {{"mode", "world"}}},
         {"components", Json::object()},
         {"prefab_instance",
          {{"asset", asset}, {"revision", prefab.revision()}, {"members", {{member.str(), id}}}}}});
    scene.edit(reconcile_prefab_intent(document, scene.prefab_sources()));
    require(scene.entity(id).has<PhysicsBody2D>() && !scene.entity(id).owns<PhysicsBody2D>(),
            "Prefab Body2D inheritance lost");
    const auto inherited = scene.document();
    command(scene, "property.set", id, "forge.physics_body_2d", "mass", 2);
    const auto intended = scene.document();
    require(row(intended, id).at("property_overrides").at("forge.physics_body_2d").at("mass") ==
                    2 &&
                !row(intended, id).at("components").contains("forge.physics_body_2d"),
            "Equal-valued Body2D override flattened prefab intent");
    require(scene.undo() && scene.document() == inherited &&
                !scene.entity(id).owns<PhysicsBody2D>(),
            "Body2D property undo lost inheritance");
    require(scene.redo() && scene.document() == intended, "Body2D property redo lost intent");
    Scene restored(engine.world());
    restored.restore_snapshot(scene.snapshot());
    require(restored.document() == intended && restored.reference(id) == scene.reference(id),
            "Body2D prefab snapshot lost identity/property intent");
    auto updated = prefab.source;
    updated["revision"] = 2u;
    auto& body = updated["members"][0]["components"]["forge.physics_body_2d"];
    body["mass"] = 3;
    body["density"] = 4;
    scene.publish_prefab_sources({{asset, updated}}, [] {});
    require(scene.entity(id).get<PhysicsBody2D>().mass == 2 &&
                scene.entity(id).get<PhysicsBody2D>().density == 4,
            "Source publication lost Body2D property boundaries");
    command(scene, "property.revert", id, "forge.physics_body_2d", "mass");
    require(!scene.entity(id).owns<PhysicsBody2D>() &&
                scene.entity(id).get<PhysicsBody2D>().mass == 3,
            "Body2D Revert retained materialization");
    require(scene.undo() && scene.entity(id).get<PhysicsBody2D>().mass == 2 && scene.redo() &&
                scene.entity(id).get<PhysicsBody2D>().mass == 3,
            "Body2D Revert history lost intent");
    const auto snapshot = scene.snapshot();
    require(snapshot.at("_prefab_sources")[0]
                    .at("members")[0]
                    .at("components")
                    .at("forge.physics_body_2d")
                    .at("future_options") == opaque,
            "Prefab materialization discarded opaque future Body2D fields");
    require(!engine.services().available(Capability::Physics), "Prefab authoring created a solver");
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Provide the build-owned scratch directory");
        standalone_commands(argv[1]);
        prefab_intent();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "2D authoring history, prefab intent/publication, identity and saved runtime "
                 "realization passed\n";
}
