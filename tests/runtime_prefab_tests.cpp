#include "authored_component.hpp"
#include "builtins.hpp"
#include "runtime_dependencies.hpp"
#include "runtime_entity_creation.hpp"
#include "runtime_package.hpp"
#include "runtime_scene_content.hpp"
#include <algorithm>
#include <array>
#include <forge/engine_assets.hpp>
#include <forge/game_content.hpp>
#include <forge/physics.hpp>
#include <forge/render_scene.hpp>
#include <forge/runtime.hpp>
#include <iostream>
using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F&& operation) {
    try {
        operation();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Expected runtime Prefab rejection");
}
Json defaults(const char* name) {
    for (const auto& type : detail::builtins())
        if (std::string_view(type.name) == name)
            return type.defaults;
    throw std::runtime_error("Missing builtin");
}
struct Fixture {
    AssetId prefab = AssetId::generate();
    PrefabMemberId root = PrefabMemberId::generate(), bob = PrefabMemberId::generate();
    Json source() const {
        auto fixed = defaults("forge.physics_body"), moving = fixed;
        moving["motion"] = 2;
        moving["mass"] = 1;
        moving["gravity_factor"] = 0;
        moving["allow_sleeping"] = false;
        auto mesh = defaults("forge.mesh_renderer");
        mesh["mesh"] = engine_primitive(0);
        auto joint = defaults("forge.physics_joint");
        joint["body_a"] = PrefabMemberRef{prefab, root};
        joint["body_b"] = PrefabMemberRef{prefab, bob};
        joint["min_distance"] = 2;
        joint["max_distance"] = 2;
        return {
            {"format", "forge.prefab"},
            {"version", 3},
            {"asset_id", prefab},
            {"revision", 1u},
            {"root", root},
            {"members",
             Json::array({{{"id", root},
                           {"name", "Anchor"},
                           {"components",
                            {{"forge.local_translation", {{"x", 0}, {"y", 0}, {"z", 0}}},
                             {"forge.physics_body", fixed},
                             {"forge.box_collider", defaults("forge.box_collider")},
                             {"forge.mesh_renderer", mesh},
                             {"forge.physics_joint", joint}}}},
                          {{"id", bob},
                           {"parent", root},
                           {"name", "Bob"},
                           {"spatial", {{"mode", "world"}}},
                           {"components",
                            {{"forge.physics_body", moving},
                             {"forge.box_collider", defaults("forge.box_collider")},
                             {"forge.mesh_renderer", mesh},
                             {"forge.local_translation", {{"x", 0}, {"y", -2}, {"z", 0}}}}}}})}};
    }
};
struct World {
    EngineContext engine{WorldRole::Runtime, false, {physics_module()}};
    Scene scene{engine.world()};
    RuntimeSimulation simulation{engine.world(), scene};
    std::shared_ptr<PhysicsRuntime> physics =
        std::static_pointer_cast<PhysicsRuntime>(engine.services().physics());
};
std::string member(const Scene& scene, EntityId root, PrefabMemberId id) {
    const auto doc = scene.document();
    for (const auto& row : doc.at("entities"))
        if (row.at("id") == root.str())
            return row.at("prefab_instance").at("members").at(id.str());
    throw std::runtime_error("Missing runtime member");
}
void exercise(const Json& snapshot, const Fixture& fixture) {
    World live;
    auto& context = live.engine.world();
    live.scene.restore_snapshot(snapshot);
    check(live.scene.entity_count() == 0, "Runtime declaration created an authored instance");
    // An unused admitted definition survives a private runtime round trip.
    const auto unused = live.scene.snapshot();
    World before_spawn;
    before_spawn.scene.restore_snapshot(unused);
    check(before_spawn.scene.prefab_sources().contains(fixture.prefab),
          "Private runtime snapshot lost an uninstantiated Prefab");
    const auto original = live.scene.document();
    const std::string owner = "project.prefab_test";
    const auto cancelled =
        detail::request_runtime_prefab(context, owner, {}, fixture.prefab, "Cancel");
    check(detail::release_runtime_entity(context, owner, cancelled), "Prefab cancellation failed");
    const auto bad =
        detail::request_runtime_prefab(context, owner, {}, AssetId::generate(), "Absent");
    const auto first = detail::request_runtime_prefab(context, owner, {}, fixture.prefab, "Left");
    const auto second = detail::request_runtime_prefab(context, owner, live.scene.asset_id(),
                                                       fixture.prefab, "Right");
    const auto reserved = detail::inspect_runtime_entity(context, owner, first).reference.entity;
    check(live.scene.document() == original, "Request mutated Scene before fixed publication");
    // A normal deferred Gameplay consumer observes Ready and places both source
    // World-space bodies explicitly before the existing physics synchronization.
    bool placed = false;
    auto system = context.world()
                      .system("test.PlaceRuntimePrefabs")
                      .kind(context.world().lookup("forge.runtime.Gameplay"))
                      .write<LocalTranslation>()
                      .run([&](flecs::iter& it) {
                          if (placed)
                              return;
                          unsigned index = 0;
                          for (auto token : {first, second}) {
                              const auto info =
                                  detail::inspect_runtime_entity(context, owner, token);
                              check(info.state == detail::RuntimeEntityState::Ready,
                                    "Prefab not Ready before Gameplay");
                              auto root = it.world().entity(info.native_entity);
                              const double x = index++ == 0 ? -4 : 4;
                              root.set<LocalTranslation>({x, 0, 0});
                              root.children([&](flecs::entity child) {
                                  it.world().entity(child.id()).set<LocalTranslation>({x, -2, 0});
                              });
                          }
                          placed = true;
                      });
    system.add<FixedSimulation>();
    live.simulation.tick(1.f / 60);
    const auto a = detail::inspect_runtime_entity(context, owner, first);
    const auto b = detail::inspect_runtime_entity(context, owner, second);
    check(a.reference.entity == reserved && a.reference != b.reference &&
              live.scene.entity_count() == 4 && !live.scene.can_undo(),
          "Prefab group identity/history is incorrect");
    check(detail::inspect_runtime_entity(context, owner, bad).state ==
              detail::RuntimeEntityState::Failed,
          "Unavailable Prefab did not fail independently");
    detail::release_runtime_entity(context, owner, bad);
    const auto a_bob = member(live.scene, a.reference.entity, fixture.bob);
    const auto b_bob = member(live.scene, b.reference.entity, fixture.bob);
    for (const auto& [root, bob] : {std::pair{a, a_bob}, std::pair{b, b_bob}}) {
        const auto native = live.scene.entity(root.reference.entity.str());
        const auto joint = native.get<PhysicsJoint>();
        check(joint.body_a == root.reference &&
                  joint.body_b == EntityRef{root.reference.scene, EntityId::parse(bob)},
              "Spawned constraint did not bind its own members");
        check(!native.owns<PhysicsBody>() && !native.owns<MeshRenderer>(),
              "Runtime creation flattened inherited components");
    }
    check(a_bob != b_bob && live.physics->status().at("joints") == 2 &&
              extract_render_scene(live.simulation.presentation(1)).meshes.size() == 4,
          "Runtime Prefabs did not reach native physics/render consumers");
    auto restored_snapshot = live.scene.snapshot();
    const auto physics = live.physics->checkpoint();
    World restored;
    restored.scene.restore_snapshot(restored_snapshot);
    restored.physics->restore(physics);
    rejects([&] { detail::inspect_runtime_entity(restored.engine.world(), owner, first); });
    for (unsigned i = 0; i < 8; ++i) {
        live.simulation.tick(1.f / 60);
        restored.simulation.tick(1.f / 60);
    }
    for (const auto& id : {a_bob, b_bob})
        check(std::abs(live.scene.entity(id).get<LocalTranslation>().x -
                       restored.scene.entity(id).get<LocalTranslation>().x) < .001,
              "Recovered created Prefab diverged");
    check(detail::release_runtime_entity(context, owner, first) &&
              context.world().is_alive(a.native_entity),
          "Releasing observation destroyed runtime group");
    context.world().entity(b.native_entity).destruct();
    check(detail::inspect_runtime_entity(context, owner, second).state ==
                  detail::RuntimeEntityState::Gone &&
              !live.scene.entity(b_bob) && live.scene.snapshot().at("entities").size() == 2 &&
              live.scene.effective_document().at("entities").size() == 2,
          "Deleted runtime root retained a Ready observation");
    detail::release_runtime_entity(context, owner, second);
    live.simulation.tick(1.f / 60);
    check(live.physics->status().at("joints") == 1 &&
              extract_render_scene(live.simulation.presentation(1)).meshes.size() == 2,
          "Destroyed group retained native physics/render participants");
    const auto later = detail::request_runtime_prefab(context, owner, {}, fixture.prefab, "Later");
    live.simulation.tick(1.f / 60);
    check(detail::inspect_runtime_entity(context, owner, later).state ==
                  detail::RuntimeEntityState::Ready &&
              live.scene.entity_count() == 4 && !live.scene.entity(b.reference.entity.str()) &&
              !live.scene.entity(b_bob),
          "Later creation resurrected a natively destroyed instance or its members");
    detail::release_runtime_entity(context, owner, later);
    system.destruct();
}
void many_member_overrides(unsigned version) {
    EngineContext engine(WorldRole::Runtime);
    Scene scene(engine.world());
    constexpr std::size_t count = 192;
    const auto asset = AssetId::generate();
    std::vector<PrefabMemberId> members;
    for (std::size_t i = 0; i < count; ++i)
        members.push_back(PrefabMemberId::generate());
    Json source{{"format", "forge.prefab"}, {"version", version},      {"asset_id", asset},
                {"revision", 1u},           {"root", members.front()}, {"members", Json::array()}};
    for (std::size_t i = 0; i < count; ++i) {
        Json item{
            {"id", members[i]},
            {"name", "Member " + std::to_string(i)},
            {"components", {{"forge.tint", {{"r", .25f}, {"g", float(i) / 512}, {"b", .75f}}}}}};
        if (i)
            item["parent"] = members.front();
        source["members"].push_back(std::move(item));
    }
    // A source is not required to place parents before children.
    std::reverse(source["members"].begin(), source["members"].end());
    scene.set_prefab_sources({{asset, source}});
    auto document = scene.document();
    document["version"] = 4;
    const auto root = EntityId::generate();
    document["entities"].push_back(
        {{"id", root},
         {"name", "Group"},
         {"components", Json::object()},
         {"prefab_instance",
          {{"asset", asset}, {"revision", 1u}, {"members", {{members.front().str(), root}}}}}});
    document = reconcile_prefab_intent(document, scene.prefab_sources(), scene.schema());
    const auto mapping = document.at("entities")[0].at("prefab_instance").at("members");
    std::map<std::string, std::size_t> ordinals;
    for (std::size_t i = 0; i < count; ++i)
        ordinals.emplace(mapping.at(members[i].str()).get<std::string>(), i);
    for (auto& item : document["entities"]) {
        const auto i = ordinals.at(item.at("id"));
        const auto red = i % 5 == 1 ? .25f : float(i) / 512;
        if (i % 3 == 0)
            item["components"]["forge.tint"] = {{"r", red}, {"g", .5f}, {"b", .75f}, {"note", i}};
        else if (i % 3 == 1)
            item["property_overrides"]["forge.tint"]["r"] = red;
    }
    scene.replace(document);
    std::vector<flecs::entity_t> handles(count);
    std::vector<Tint> expected(count);
    for (const auto& [id, i] : ordinals) {
        auto native = scene.entity(id);
        handles[i] = native.id();
        auto value = native.get<Tint>();
        const auto red = i % 5 == 1 ? .25f : float(i) / 512;
        check(value.r == (i % 3 == 2 ? .25f : red) &&
                  value.g == (i % 3 == 0 ? .5f : float(i) / 512) &&
                  native.owns<Tint>() == (i % 3 != 2),
              "Many-member projection mixed values or changed inheritance");
        if (i % 3 != 2) {
            value.b = .125f + float(i) / 1024;
            native.set<Tint>(value);
        }
        expected[i] = value;
    }
    const auto effective = scene.effective_document();
    for (const auto& item : effective.at("entities")) {
        const auto i = ordinals.at(item.at("id"));
        const auto& tint = item.at("components").at("forge.tint");
        check(tint.at("r") == expected[i].r && tint.at("g") == expected[i].g &&
                  tint.at("b") == expected[i].b,
              "Effective whole-component projection lost native fields");
        if (i % 3 == 0)
            check(tint.at("note") == i, "Effective projection lost a component extension");
    }
    const auto token = detail::request_runtime_entity(engine.world(), "project.many", {}, "Later");
    detail::publish_runtime_entities(engine.world());
    check(detail::inspect_runtime_entity(engine.world(), "project.many", token).state ==
                  detail::RuntimeEntityState::Ready &&
              scene.entity_count() == count + 1,
          "Unrelated append failed for many-member overrides");
    detail::release_runtime_entity(engine.world(), "project.many", token);
    const auto saved = scene.document();
    for (const auto& item : saved.at("entities")) {
        if (!ordinals.contains(item.at("id")))
            continue;
        const auto i = ordinals.at(item.at("id"));
        check(scene.entity(item.at("id")).id() == handles[i] &&
                  scene.entity(item.at("id")).get<Tint>() == expected[i],
              "Append changed a surviving member handle or native value");
        if (i % 3 == 0)
            check(item.at("components").at("forge.tint").at("note") == i,
                  "Whole-component extension was lost");
        else if (i % 3 == 1)
            check(item.at("property_overrides").at("forge.tint").contains("r") &&
                      item.at("property_overrides").at("forge.tint").contains("b") &&
                      !item.at("components").contains("forge.tint"),
                  "Equal mask or unmasked native edit lost authored intent");
    }
    EngineContext restored_engine(WorldRole::Runtime);
    Scene restored(restored_engine.world());
    restored.restore_snapshot(scene.snapshot());
    for (const auto& [id, i] : ordinals)
        check(restored.entity(id).get<Tint>() == expected[i] &&
                  restored.entity(id).owns<Tint>() == (i % 3 != 2),
              "Many-member recovery changed values or inheritance");
    check(scene.prefab_sources().at(asset) == source, "Runtime append mutated Prefab source");
}
void publication_budget() {
    const auto start = std::chrono::steady_clock::now();
    auto mark = [&](const char* phase) {
        std::cerr << "4096-member creation " << phase << ": "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - start)
                         .count()
                  << " ms cumulative\n";
    };
    EngineContext engine(WorldRole::Runtime);
    Scene scene(engine.world());
    const auto asset = AssetId::generate();
    const auto root = PrefabMemberId::generate();
    Json members = Json::array({{{"id", root}, {"name", "Root"}, {"components", Json::object()}}});
    for (unsigned i = 1; i < 4096; ++i)
        members.push_back({{"id", PrefabMemberId::generate()},
                           {"parent", root},
                           {"name", "Member"},
                           {"components", Json::object()}});
    scene.set_prefab_sources({{asset,
                               {{"format", "forge.prefab"},
                                {"version", 3},
                                {"asset_id", asset},
                                {"revision", 1u},
                                {"root", root},
                                {"members", members}}}});
    mark("templates admitted");
    const auto group = detail::request_runtime_prefab(engine.world(), "project.budget", {}, asset,
                                                      "Maximum group");
    const auto empty =
        detail::request_runtime_entity(engine.world(), "project.budget", {}, "Later entity");
    detail::publish_runtime_entities(engine.world());
    mark("group published");
    check(scene.entity_count() == 4096 &&
              detail::inspect_runtime_entity(engine.world(), "project.budget", group).state ==
                  detail::RuntimeEntityState::Ready &&
              detail::inspect_runtime_entity(engine.world(), "project.budget", empty).state ==
                  detail::RuntimeEntityState::Pending,
          "Creation boundary exceeded its row budget or split a Prefab group");
    detail::publish_runtime_entities(engine.world());
    mark("delayed entity published");
    check(scene.entity_count() == 4097 &&
              detail::inspect_runtime_entity(engine.world(), "project.budget", empty).state ==
                  detail::RuntimeEntityState::Ready,
          "Budget-delayed request starved at the next boundary");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2 || (argc == 3 && std::string_view(argv[2]) == "--budget"),
              "Need runtime Prefab scratch directory and optional --budget");
        if (argc == 3) {
            publication_budget();
            std::cout << "4096-member atomic group and next-boundary budget passed\n";
            return 0;
        }
        for (const auto version : {1u, 2u, 3u})
            many_member_overrides(version);
        Fixture fixture;
        const auto scratch = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        const auto root = scratch / "source";
        std::filesystem::create_directories(root);
        WorldContext authoring;
        Scene authored(authoring);
        authored.save(root / "spawn.scene.json");
        atomic_write(root / "rig.prefab.json", fixture.source().dump(2));
        AssetCatalog catalog(root);
        const auto scene_record = catalog.add_scene("spawn.scene.json");
        catalog.add({fixture.prefab, PrefabAsset::type, "rig.prefab.json", 3});
        catalog.save(AssetCatalog::project_index(root));
        check(load_game_scene(root, {scene_record.id}).at("_prefab_sources").empty(),
              "Undeclared unused Prefab was discovered by filesystem scan");
        {
            ProjectLease lease(root);
            catalog = declare_runtime_dependencies(lease, scene_record.id,
                                                   {{fixture.prefab,
                                                     PrefabAsset::type,
                                                     AssetDependencyKind::Runtime,
                                                     "declared:spawn-rig",
                                                     {}}},
                                                   catalog.document());
        }
        exercise(load_game_scene(root, {scene_record.id}), fixture);
        auto edited = authored.snapshot();
        edited["note"] = "unsaved scene data";
        const auto prepared = detail::runtime_scene_content(root, edited);
        check(prepared.at("note") == edited.at("note") &&
                  prepared.at("entities") == edited.at("entities") &&
                  prepared.at("_prefab_sources").size() == 1,
              "Play preparation replaced caller scene edits or lost declarations");
        auto supplied = edited;
        auto explicit_source = fixture.source();
        explicit_source["revision"] = 2;
        supplied["_prefab_sources"] = Json::array({explicit_source});
        check(detail::runtime_scene_content(root, supplied).at("_prefab_sources")[0] ==
                  explicit_source,
              "Play preparation replaced an explicit caller definition");
        supplied["_prefab_sources"].push_back(explicit_source);
        rejects([&] { detail::runtime_scene_content(root, supplied); });
        const std::array roots{scene_record.id};
        const auto output = scratch / "export";
        package_runtime_content(root, output, roots, {"windows", "d3d12"});
        const auto relocated = scratch / "relocated";
        std::filesystem::rename(output, relocated);
        std::filesystem::rename(root, scratch / "source-offline");
        const auto shipped = open_runtime_content(relocated, {"windows", "d3d12"});
        check(shipped.records().size() == 2 && shipped.records().contains(fixture.prefab),
              "Runtime-only declared Prefab missing from export");
        exercise(load_game_scene(relocated, {scene_record.id}), fixture);
        std::cout << "Runtime Prefab queue, native consumers, recovery and relocated "
                     "Windows-target data passed on Linux\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
