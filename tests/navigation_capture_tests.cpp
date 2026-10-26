#include "navigation_sources.hpp"
#include <cstddef>
#include <forge/authoring.hpp>
#include <forge/character_components.hpp>
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
    floor.set<CharacterController2D>({});
    f.equivalent("enabled2D controller source", false);
    floor.remove<CharacterController2D>();
    floor.set<CharacterController>({});
    f.equivalent("enabled3D controller source", false);
    floor.set<CharacterController>({false});
    f.equivalent("disabled3D controller source");
    floor.remove<CharacterController>();
    floor.set<CharacterController2D>({false});
    f.equivalent("disabled2D controller source");
    floor.remove<CharacterController2D>();
    for (bool planar : {false, true}) {
        f.scene.reparent_entity("floor", "parent", ReparentMode::KeepLocal);
        if (planar)
            parent.set<CharacterController2D>({});
        else
            parent.set<CharacterController>({});
        f.equivalent(planar ? "2D controller structural ancestor"
                            : "3D controller structural ancestor",
                     false);
        floor.set<SpatialBinding>({SpatialMode::Explicit, f.scene.reference("parent")});
        f.equivalent(planar ? "2D controller explicit ancestor" : "3D controller explicit ancestor",
                     false);
        floor.set<SpatialBinding>({SpatialMode::World});
        f.equivalent("World binding ignores controller structural ancestor");
        floor.set<SpatialBinding>({SpatialMode::Explicit, f.scene.reference("parent")});
        if (planar)
            parent.set<CharacterController2D>({false});
        else
            parent.set<CharacterController>({false});
        f.equivalent("Disabled controller ancestor");
        parent.remove<CharacterController>();
        parent.remove<CharacterController2D>();
        floor.set<SpatialBinding>({SpatialMode::FollowStructure});
        f.scene.reparent_entity("floor", "", ReparentMode::KeepLocal);
    }

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
void planar_capture_tests() {
    Fixture f;
    auto floor = f.scene.entity("floor");
    floor.remove<NavigationSurface>();
    floor.set<LocalScale>({1, 1, 0});
    floor.set<NavigationRegion2D>({true, false, 20, 20});
    auto compare = [&] {
        const auto before = f.scene.document();
        const auto source =
            navigation_detail::capture(f.scene.effective_document(), NavigationPlane::PlanarXY);
        const auto native = navigation_detail::capture_native(
            f.engine.world(), f.scene, f.candidates, NavigationPlane::PlanarXY);
        const auto a = navigation_detail::geometry(source), b = navigation_detail::geometry(native);
        check(a.digest == b.digest && a.vertices == b.vertices && a.obstacles == b.obstacles &&
                  source.identities == native.identities && source.meshes.empty() &&
                  native.meshes.empty(),
              "XY native capture differs from authored region geometry");
        check(f.scene.document() == before, "XY source capture authored scene values");
    };
    compare();
    auto region = floor.get<NavigationRegion2D>();
    region.width = 2;
    region.height = 4;
    region.obstacle = true;
    floor.set<NavigationRegion2D>(region);
    f.scene.entity("parent").set<NavigationRegion2D>({true, false, 20, 20});
    compare();
    auto component = f.engine.world().world().lookup("forge.navigation_region_2d");
    const auto* info = ecs_get_type_info(f.engine.world().world().c_ptr(), component.id());
    check(info && std::size_t(info->size) == sizeof(NavigationRegion2D),
          "XY region has wrong reflected native size");
    for (const auto& [name, offset] :
         {std::pair{"enabled", offsetof(NavigationRegion2D, enabled)},
          std::pair{"obstacle", offsetof(NavigationRegion2D, obstacle)},
          std::pair{"width", offsetof(NavigationRegion2D, width)},
          std::pair{"height", offsetof(NavigationRegion2D, height)}}) {
        const auto* member =
            ecs_struct_get_member(f.engine.world().world().c_ptr(), component.id(), name);
        check(member && std::size_t(member->offset) == offset,
              "XY region has wrong reflected member offset");
    }
    const auto selected = floor.get<PersistentEntityId>().value.str();
    authoring_command(f.scene, "property.set",
                      {{"entity", selected},
                       {"component", "forge.navigation_region_2d"},
                       {"field", "width"},
                       {"value", 3}});
    check(f.scene.entity(selected).get<NavigationRegion2D>().width == 3 && f.scene.undo() &&
              f.scene.entity(selected).get<NavigationRegion2D>().width == 2 && f.scene.redo() &&
              f.scene.entity(selected).get<NavigationRegion2D>().width == 3,
          "XY region property edit lost native value/Undo/Redo");
    compare();
    const auto before_invalid = f.scene.document();
    bool rejected = false;
    try {
        authoring_command(f.scene, "property.set",
                          {{"entity", selected},
                           {"component", "forge.navigation_region_2d"},
                           {"field", "height"},
                           {"value", 0}});
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected && f.scene.document() == before_invalid,
          "Invalid XY region dimensions changed the scene");
    f.equivalent("XY region is not an implicit3D navigation source", false);
    std::cout << "XY source native/schema/history/validation PASS\n";
}
void controller_prefab_tests() {
    for (bool planar : {false, true}) {
        Fixture f;
        auto floor = f.scene.entity("floor");
        if (planar)
            floor.set<CharacterController2D>({});
        else
            floor.set<CharacterController>({});
        auto source = create_prefab_source(f.scene, "floor");
        f.scene.set_prefab_sources({{source.asset(), source.source}});
        if (planar)
            floor.set<CharacterController2D>({false});
        else
            floor.set<CharacterController>({false});
        const auto instance = instantiate_prefab(f.scene, source.asset());
        auto inherited = f.scene.entity(instance);
        check(planar ? !inherited.owns<CharacterController2D>()
                     : !inherited.owns<CharacterController>(),
              "Controller source fixture did not retain actual prefab inheritance");
        f.equivalent("Inherited enabled controller source", false);
        authoring_command(
            f.scene, "property.set",
            {{"entity", instance},
             {"component", planar ? "forge.character_controller_2d" : "forge.character_controller"},
             {"field", "enabled"},
             {"value", false}});
        f.equivalent("Partial disabled controller source override");
    }
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
                if (member != source.source.at("root").get_ref<const std::string&>())
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
    auto candidates = query(engine.world());
    const auto native = navigation_detail::capture_native(engine.world(), scene, candidates);
    check(navigation_detail::selected_geometry_digest(root, native, record.metadata) ==
                  record.metadata.at("geometry_sha256").get_ref<const std::string&>() &&
              navigation_detail::selected_geometry_digest(root, native, record.metadata) ==
                  navigation_geometry_digest(root, scene.effective_document(), record.metadata),
          "Native selected provenance differs from baked/document source");
    check(navigation_detail::selected_geometry_digest(root, native, Json{{"version", 1}}) ==
              navigation_geometry_digest(scene.effective_document()),
          "Native status did not preserve legacy provenance");
    EngineContext foreign;
    bool rejected = false;
    try {
        navigation_detail::capture_native(foreign.world(), scene, candidates);
    } catch (const std::logic_error&) {
        rejected = true;
    }
    check(rejected, "Foreign context evaluated a Scene's navigation sources");
    RuntimeSimulation simulation(engine.world(), scene);
    auto nav = std::static_pointer_cast<NavigationRuntime>(engine.services().navigation());
    check(nav->project_point({record.id}, {0, .1, 0}).status == NavStatus::Success,
          "Native capture lost admitted route");
    scene.entity("floor").set<NavigationRegion2D>({true, false, 0, 0});
    check(nav->project_point({record.id}, {0, .1, 0}).status == NavStatus::Success,
          "Invalid unselected XY geometry poisoned the spatial navigation profile");
    scene.entity("floor").remove<NavigationRegion2D>();
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
        planar_capture_tests();
        controller_prefab_tests();
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
