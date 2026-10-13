#include "navigation_sources.hpp"
#include <forge/authoring.hpp>
#include <forge/navigation.hpp>
#include <forge/prefab_authoring.hpp>
#include <forge/runtime.hpp>
#include <iostream>

using namespace forge;
namespace {
void check(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
Json input() {
    return {{"version", 1},
            {"entities", Json::array({{{"id", "floor"},
                                       {"name", "Floor"},
                                       {"components",
                                        {{"forge.position", {{"x", 0}, {"y", 0}, {"z", 0}}},
                                         {"forge.scale", {{"x", 20}, {"y", 1}, {"z", 20}}},
                                         {"forge.primitive", {{"kind", 3u}}},
                                         {"forge.navigation_surface", {{"enabled", true}}}}}},
                                      {{"id", "parent"},
                                       {"name", "Group"},
                                       {"components",
                                        {{"forge.position", {{"x", 0}, {"y", 0}, {"z", 0}}},
                                         {"forge.primitive", {{"kind", no_primitive}}}}}}})}};
}
auto query(WorldContext& context) {
    return context.world()
        .query_builder<const PersistentEntityId>()
        .cache_kind(flecs::QueryCacheNone)
        .query_flags(EcsQueryMatchPrefab | EcsQueryMatchDisabled)
        .build();
}
struct Fixture {
    EngineContext engine;
    Scene scene{engine.world()};
    flecs::query<const PersistentEntityId> candidates{query(engine.world())};
    Fixture() { scene.restore_snapshot(input()); }
    void equivalent(const char* label, bool valid = true) {
        std::cout << "Capture oracle: " << label << std::endl;
        const auto authored = scene.document();
        std::string old_error, native_error;
        std::optional<navigation_detail::Sources> old_source, native_source;
        try {
            old_source = navigation_detail::capture(scene.effective_document());
        } catch (const std::exception& e) {
            old_error = e.what();
        }
        try {
            native_source = navigation_detail::capture_native(engine.world(), scene, candidates);
        } catch (const std::exception& e) {
            native_error = e.what();
        }
        check(old_error == native_error,
              std::string(label) + ": diagnostics differ: " + old_error + " / " + native_error);
        check(bool(old_source) == valid && bool(native_source) == valid,
              std::string(label) + ": expected source validity differs");
        if (old_source) {
            auto inputs = navigation_detail::mesh_inputs(
                *old_source, [](AssetId) { return std::string(64, 'a'); });
            check(old_source->identities == native_source->identities &&
                      old_source->meshes == native_source->meshes &&
                      navigation_detail::source_digest(*old_source, inputs) ==
                          navigation_detail::source_digest(*native_source, inputs),
                  std::string(label) + ": v2 sources differ");
            if (old_source->meshes.empty()) {
                const auto a = navigation_detail::geometry(*old_source);
                const auto b = navigation_detail::geometry(*native_source);
                check(a.digest == b.digest && a.vertices == b.vertices && a.indices == b.indices &&
                          a.digest == navigation_geometry_digest(scene.effective_document()),
                      std::string(label) + ": v1 canonical geometry differs");
            }
        }
        check(scene.document() == authored, std::string(label) + ": capture authored values");
    }
};
void source_tests() {
    Fixture f;
    auto floor = f.scene.entity("floor");
    auto parent = f.scene.entity("parent");
    f.equivalent("plain primitive");
    floor.set<LocalScale>({-20, 1, 20});
    f.equivalent("mirrored primitive");
    floor.set<MeshRenderer>({{AssetId::generate()}});
    f.equivalent("selected Mesh reference");
    auto mesh = floor.get<MeshRenderer>();
    mesh.enabled = false;
    floor.set<MeshRenderer>(mesh);
    f.equivalent("disabled Mesh overrides primitive", false);
    floor.remove<MeshRenderer>();
    floor.set<NavigationSurface>({false});
    f.equivalent("disabled surface", false);
    floor.remove<NavigationSurface>();
    f.equivalent("removed surface", false);
    floor.set<NavigationSurface>({true});
    floor.remove<Primitive>();
    f.equivalent("group has no geometry", false);
    floor.set<Primitive>({3});
    floor.set<PhysicsBody>({2});
    f.equivalent("moving source", false);
    floor.remove<PhysicsBody>();
    f.scene.reparent_entity("floor", "parent", ReparentMode::KeepLocal);
    parent.set<PhysicsBody>({1});
    f.equivalent("moving structural ancestor", false);
    floor.set<SpatialBinding>({SpatialMode::World});
    f.equivalent("world binding ignores moving structural ancestor");
    floor.set<SpatialBinding>({SpatialMode::FollowStructure});
    parent.remove<PhysicsBody>();
    parent.set<NavigationAgent>({});
    f.equivalent("enabled agent ancestor", false);
    auto agent = parent.get<NavigationAgent>();
    agent.enabled = false;
    parent.set<NavigationAgent>(agent);
    f.equivalent("disabled agent ancestor");
    f.scene.reparent_entity("floor", "", ReparentMode::KeepLocal);
    floor = f.scene.entity("floor");
    floor.set<SpatialBinding>({SpatialMode::Explicit, f.scene.reference("parent")});
    f.equivalent("explicit static ancestry");
    floor.set<SpatialBinding>({SpatialMode::Explicit, {AssetId::generate(), EntityId::generate()}});
    f.equivalent("foreign explicit ancestry", false);
    floor.set<SpatialBinding>({SpatialMode::Explicit, {f.scene.asset_id(), EntityId::generate()}});
    f.equivalent("missing explicit ancestry", false);
    floor.set<SpatialBinding>({SpatialMode::Explicit, f.scene.reference("floor")});
    f.equivalent("explicit spatial cycle", false);
    floor.set<SpatialBinding>({SpatialMode::World});
    floor.disable();
    f.equivalent("retained disabled entity source");
    floor.enable();
    // Whole-world queries must not capture another instance of the same asset.
    Scene second(f.engine.world());
    second.restore_snapshot(f.scene.snapshot());
    second.entity("floor").set<LocalTranslation>({100, 0, 0});
    f.equivalent("same-asset scene instance isolation");
    auto untracked = f.engine.world().world().entity();
    untracked.set<PersistentEntityId>({EntityId::generate()})
        .set<NavigationSurface>({true})
        .set<Primitive>({0})
        .set<LocalTranslation>({0, 0, 0});
    f.equivalent("untracked native entity excluded");
    untracked.destruct();
    f.scene.delete_subtree("floor");
    f.equivalent("deleted source", false);
}
void prefab_tests() {
    Fixture f;
    auto source = create_prefab_source(f.scene, "floor");
    f.scene.set_prefab_sources({{source.asset(), source.source}});
    const auto instance = instantiate_prefab(f.scene, source.asset());
    f.equivalent("structured inherited surface");
    authoring_command(
        f.scene, "property.set",
        {{"entity", instance}, {"component", "forge.local_scale"}, {"field", "x"}, {"value", -10}});
    authoring_command(f.scene, "property.set",
                      {{"entity", instance},
                       {"component", "forge.navigation_surface"},
                       {"field", "enabled"},
                       {"value", false}});
    f.equivalent("partial prefab masks");
    auto changed = source.source;
    changed["revision"] = 2;
    for (auto& member : changed["members"])
        if (member["id"] == changed["root"])
            member["components"]["forge.local_scale"]["z"] = 12;
    f.scene.publish_prefab_sources({{source.asset(), changed}}, [] {});
    f.equivalent("published inherited channels");
    authoring_command(
        f.scene, "property.revert",
        {{"entity", instance}, {"component", "forge.navigation_surface"}, {"field", "enabled"}});
    f.equivalent("reverted surface mask");
    check(f.scene.undo() && f.scene.redo(), "Prefab history unavailable");
    f.equivalent("prefab history replay");

    Fixture templates;
    auto doc = templates.scene.document();
    const auto child = EntityId::generate().str();
    doc["entities"][1]["prefab"] = true;
    doc["entities"].push_back({{"id", child},
                               {"name", "Implicit template child"},
                               {"parent", doc["entities"][1]["id"]},
                               {"components", doc["entities"][0]["components"]}});
    f.scene.restore_snapshot(doc);
    check(f.scene.entity(child).has(flecs::Prefab), "Missing implicit template");
    f.equivalent("implicit template child retained by source contract");
    f.scene.entity(child).disable();
    f.equivalent("disabled implicit template child");
}
void unrelated_tests() {
    Fixture f;
    auto doc = f.scene.document();
    doc["opaque_workload"] = std::string(4 * 1024 * 1024, 'x');
    for (unsigned i = 0; i < 1024; ++i)
        doc["entities"].push_back({{"id", EntityId::generate()},
                                   {"name", "Unmarked"},
                                   {"components",
                                    {{"forge.local_translation", {{"x", i}, {"y", 0}, {"z", 0}}},
                                     {"forge.primitive", {{"kind", no_primitive}}}}}});
    f.scene.restore_snapshot(doc);
    f.equivalent("large unrelated scene");
}
void removed_prefab_test(bool own_geometry) {
    Fixture f;
    f.scene.reparent_entity("floor", "parent", ReparentMode::KeepLocal);
    auto source = create_prefab_source(f.scene, "parent");
    f.scene.set_prefab_sources({{source.asset(), source.source}});
    const auto instance = instantiate_prefab(f.scene, source.asset());
    const auto doc = f.scene.document();
    std::string child;
    for (const auto& row : doc.at("entities"))
        if (row.at("id") == instance)
            for (const auto& [member, id] : row.at("prefab_instance").at("members").items())
                if (member != source.source.at("root"))
                    child = id.get<std::string>();
    check(!child.empty(), "Missing prefab child fixture");
    authoring_command(f.scene, "component.override",
                      {{"entity", child}, {"component", "forge.navigation_surface"}});
    if (own_geometry) {
        authoring_command(f.scene, "component.add",
                          {{"entity", child}, {"component", "forge.mesh_renderer"}});
        authoring_command(f.scene, "property.set",
                          {{"entity", child},
                           {"component", "forge.mesh_renderer"},
                           {"field", "mesh"},
                           {"value", engine_assets()[3].id}});
    }
    f.equivalent("owned child surface");
    auto removed = source.source;
    removed["revision"] = 2;
    removed["members"] = Json::array();
    for (const auto& member : source.source.at("members"))
        if (member.at("id") == source.source.at("root"))
            removed["members"].push_back(member);
    f.scene.publish_prefab_sources({{source.asset(), removed}}, [] {});
    check(!f.scene.entity(child), "Removed prefab child remained native");
    f.equivalent("removed member with owned surface intent", false);
    auto disabled = f.scene.document();
    for (auto& row : disabled["entities"])
        if (row.at("id") == child)
            row["components"]["forge.navigation_surface"]["enabled"] = false;
    f.scene.edit(disabled);
    f.equivalent("removed member with disabled surface intent");
}
void runtime_tests(const std::filesystem::path& root, const std::filesystem::path& worker) {
    EngineContext engine(WorldRole::Runtime, false, {navigation_module(root)});
    Scene scene(engine.world());
    scene.restore_snapshot(input());
    auto prepared = prepare_navigation(root, scene.effective_document(), {}, worker);
    const auto record = prepared.publish(scene.effective_document());
    RuntimeSimulation simulation(engine.world(), scene);
    auto nav = std::static_pointer_cast<NavigationRuntime>(engine.services().navigation());
    check(nav->project_point({record.id}, {0, .1, 0}).status == NavStatus::Success,
          "Native capture lost admitted route");
    for (int i = 0; i < 8; ++i)
        nav->synchronize();
    check(engine.services().profiles().empty(), "Disabled capture profiling collected data");
    engine.services().profiling(true);
    nav->synchronize();
#ifndef FORGE_DISABLE_PROFILING
    unsigned captures = 0;
    for (const auto& sample : engine.services().profiles())
        if (sample.at("name") == "NavSourceCapture") {
            ++captures;
            check(sample.at("category") == "navigation" && sample.at("tick") == 0 &&
                      sample.at("seconds").get<double>() >= 0,
                  "Invalid capture profile record");
        }
    check(captures == 1, "Missing navigation capture profile");
#endif
    engine.services().profiling(false);
    auto floor = scene.entity("floor");
    auto& local = floor.get_mut<LocalTranslation>();
    ecs_readonly_begin(engine.world().world().c_ptr(), false);
    local.x = 1;
    check(nav->project_point({record.id}, {0, .1, 0}).status == NavStatus::Success,
          "Readonly query recaptured gameplay writes");
    ecs_readonly_end(engine.world().world().c_ptr());
    floor.modified<LocalTranslation>();
    check(nav->project_point({record.id}, {0, .1, 0}).status == NavStatus::Stale,
          "Writable capture failed to invalidate changed geometry");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3 || (argc == 4 && std::string_view(argv[3]) == "--equivalence-only"),
              "Need data root and navigation worker");
        source_tests();
        prefab_tests();
        removed_prefab_test(false);
        removed_prefab_test(true);
        unrelated_tests();
        if (argc == 4)
            return 0;
        const auto root = std::filesystem::path(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        runtime_tests(root, std::filesystem::absolute(argv[2]));
        std::filesystem::remove_all(root);
        std::cout << "Native navigation capture equivalence and runtime boundaries passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
