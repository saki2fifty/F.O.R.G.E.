#include <cmath>
#include <forge/assets.hpp>
#include <forge/authoring.hpp>
#include <forge/navigation.hpp>
#include <forge/navigation_build.hpp>
#include <forge/prefab_authoring.hpp>
#include <forge/runtime.hpp>
#include <fstream>
#include <iostream>
using namespace forge;
void check(bool v, const char* why) {
    if (!v)
        throw std::runtime_error(why);
}
template <class F> void reject(F f) {
    try {
        f();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Expected rejection");
}
std::string read(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
Json input() {
    return {
        {"version", 1},
        {"entities",
         Json::array({{{"id", "floor"},
                       {"name", "Floor"},
                       {"components",
                        {{"forge.position", {{"x", 0}, {"y", 0}, {"z", 0}}},
                         {"forge.scale", {{"x", 20}, {"y", 1}, {"z", 20}}},
                         {"forge.primitive", {{"kind", 3u}}}}}},
                      {{"id", "obstacle"},
                       {"name", "Obstacle"},
                       {"components",
                        {{"forge.position", {{"x", 0}, {"y", 2}, {"z", 0}}},
                         {"forge.scale", {{"x", 2}, {"y", 4}, {"z", 4}}},
                         {"forge.primitive", {{"kind", 0u}}}}}},
                      {{"id", "agent"},
                       {"name", "Agent"},
                       {"components", {{"forge.position", {{"x", -8}, {"y", .1}, {"z", 0}}}}}}})}};
}
struct Fixture {
    Module module;
    EngineContext engine;
    Scene scene;
    RuntimeSimulation simulation;
    std::shared_ptr<NavigationRuntime> navigation;
    Fixture(const std::filesystem::path& root)
        : engine(WorldRole::Runtime, false, {navigation_module(root)}), scene(engine.world()),
          simulation(engine.world(), scene, module),
          navigation(
              std::static_pointer_cast<NavigationRuntime>(engine.world().services().navigation())) {
    }
    void tick() { simulation.tick(1.f / 60); }
    Double3 position() {
        engine.world().evaluate_world_transforms();
        return scene.entity("agent").get<WorldTransform>().affine.point({0, 0, 0});
    }
};
int main(int argc, char** argv) {
    try {
        check((argc == 3 || argc == 4), "Need test root and worker");
        auto root = std::filesystem::path(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        EngineContext author;
        Scene scene(author.world());
        scene.restore_snapshot(input());
        scene.entity("floor").set<NavigationSurface>({true});
        scene.entity("obstacle").set<NavigationSurface>({true});
        auto effective = scene.effective_document();
        auto prepare = [&](NavigationSettings s = NavigationSettings{}) {
            return prepare_navigation(root, scene.effective_document(), s,
                                      std::filesystem::absolute(argv[2]));
        };
        auto record = prepare().publish(effective);
        check(record.dependency_edges.size() == 1 &&
                  record.dependency_edges.front().kind == AssetDependencyKind::Build &&
                  record.dependency_edges.front().expected_type == "scene" &&
                  record.dependency_edges.front().target == effective.at("asset_id").get<AssetId>(),
              "Navigation lost its typed source-scene dependency");
        auto baseline = read(AssetCatalog::project_index(root));
        check(navigation_triangles(root, {record.id}).size() > 0, "Missing debug mesh");
        auto candidate = prepare();
        scene.entity("obstacle").set<LocalTranslation>({1, 2, 0});
        reject([&] { candidate.publish(scene.effective_document()); });
        check(read(AssetCatalog::project_index(root)) == baseline,
              "Stale candidate changed catalog");
        scene.entity("obstacle").set<LocalTranslation>({0, 2, 0});
        reject([&] { prepare({.4f, 2, .4f, 45, .001f, .1f}); });
        check(read(AssetCatalog::project_index(root)) == baseline, "Bad settings changed catalog");
        std::stop_source cancelled;
        cancelled.request_stop();
        reject([&] {
            prepare_navigation(root, scene.effective_document(), {},
                               std::filesystem::absolute(argv[2]), cancelled.get_token());
        });
        check(read(AssetCatalog::project_index(root)) == baseline,
              "Cancelled navigation changed catalog");
        auto updated = prepare({.5f, 2, .4f, 45, .2f, .1f}).publish(scene.effective_document());
        check(updated.id == record.id && updated.source != record.source,
              "Rebuild did not preserve logical identity/new artifact");
        check(std::filesystem::exists(root / record.source), "Previous good artifact removed");
        record = updated;
        scene.entity("agent").set<NavigationAgent>({{record.id}, true, true, 2, .1f, 8, .1, 0});
        auto snapshot = scene.snapshot();
        if (argc == 4) {
            check(std::string(argv[3]) == "--prepare", "Unknown mode");
            std::ofstream(root / "scene.json") << snapshot.dump();
            std::cout << std::filesystem::absolute(root).string() << "\n";
            return 0;
        }
        Fixture f(root);
        f.scene.restore_snapshot(snapshot);
        f.simulation.reset_presentation();
        check(f.navigation->project_point({record.id}, {-8, .1, 0}).status == NavStatus::Success,
              "Headless projection failed");
        check(f.navigation->find_path({record.id}, {-8, .1, 0}, {8, .1, 0}).points.size() > 2,
              "Headless route failed");
        auto start = f.position();
        f.tick();
        auto next = f.position();
        check(std::hypot(next[0] - start[0], next[2] - start[2]) > .02 &&
                  std::hypot(next[0] - start[0], next[2] - start[2]) < .04,
              "Fixed step speed wrong");
        const auto artifact = root / record.source;
        auto good_bytes = read(artifact);
        std::ofstream(artifact, std::ios::binary) << "invalid";
        check(f.navigation->project_point({record.id}, {-8, .1, 0}).status == NavStatus::Success,
              "Admitted immutable cache lost last good bytes");
        Fixture corrupt(root);
        corrupt.scene.restore_snapshot(snapshot);
        check(corrupt.navigation->project_point({record.id}, {-8, .1, 0}).status ==
                  NavStatus::Invalid,
              "Corrupt artifact reached Detour");
        std::ofstream(artifact, std::ios::binary) << good_bytes;
        check(read(artifact) == good_bytes, "Binary fixture restoration changed bytes");
        auto checkpoint = f.navigation->checkpoint();
        auto mid = f.scene.snapshot();
        Fixture recovered(root);
        recovered.scene.restore_snapshot(mid);
        recovered.navigation->restore(checkpoint);
        recovered.tick();
        f.tick();
        check(std::hypot(f.position()[0] - recovered.position()[0],
                         f.position()[2] - recovered.position()[2]) < .001,
              "Recovery recomputation changed motion");
        bool avoided = false;
        for (int i = 0; i < 600; ++i) {
            f.tick();
            auto p = f.position();
            if (std::abs(p[0]) < 1.4) {
                check(std::abs(p[2]) > 2.4, "Agent crossed obstacle");
                avoided = true;
            }
        }
        check(avoided && std::hypot(f.position()[0] - 8, f.position()[2]) < .12,
              "Agent did not reach stopping distance");
        auto stop = f.position();
        for (int i = 0; i < 10; ++i)
            f.tick();
        check(f.position() == stop, "Agent did not stop");
        auto a = f.scene.entity("agent").get<NavigationAgent>();
        a.destination_x = -8;
        f.scene.entity("agent").set<NavigationAgent>(a);
        f.tick();
        check(f.position() != stop, "Destination change not followed");
        auto body = PhysicsBody{};
        f.scene.entity("agent").set<PhysicsBody>(body);
        stop = f.position();
        f.tick();
        check(f.position() == stop, "Navigation drove physics body");
        f.scene.entity("agent").remove<PhysicsBody>();
        f.scene.entity("floor").set<LocalTranslation>({0, 1, 0});
        check(f.navigation->project_point({record.id}, {-8, .1, 0}).status == NavStatus::Stale,
              "Changed source not stale");
        stop = f.position();
        f.tick();
        check(f.position() == stop, "Stale navmesh moved agent");

        f.scene.entity("floor").set<LocalTranslation>({0, 0, 0});
        const auto floor_surface = f.scene.entity("floor").get<NavigationSurface>();
        const auto obstacle_surface = f.scene.entity("obstacle").get<NavigationSurface>();
        f.scene.entity("floor").set<NavigationSurface>({false});
        f.scene.entity("obstacle").set<NavigationSurface>({false});
        check(f.navigation->project_point({record.id}, {-8, .1, 0}).status == NavStatus::Stale,
              "Disabling all native surfaces reused previous geometry");
        f.scene.entity("floor").set<NavigationSurface>(floor_surface);
        f.scene.entity("obstacle").set<NavigationSurface>(obstacle_surface);
        check(f.navigation->project_point({record.id}, {-8, .1, 0}).status == NavStatus::Success,
              "Re-enabling native surfaces did not refresh geometry");
        auto bad_checkpoint = checkpoint;
        bad_checkpoint["assets"][0]["sha256"] = "wrong";
        reject([&] { recovered.navigation->restore(bad_checkpoint); });
        auto parent_id = authoring_command(f.scene, "entity.create", {{"name", "Moving ancestor"}})
                             .at("selected")
                             .get<std::string>();
        auto parent = f.scene.entity(parent_id);
        PhysicsBody moving;
        moving.motion = 2;
        parent.set<PhysicsBody>(moving);
        auto agent = f.scene.entity("agent");
        agent.set<SpatialBinding>(
            {SpatialMode::Explicit, f.engine.world().reference(parent.id()).value()});
        stop = f.position();
        f.tick();
        check(f.position() == stop, "Agent followed Dynamic spatial ancestor");
        agent.add(flecs::ChildOf, parent);
        agent.set<SpatialBinding>({SpatialMode::World, {}});
        f.tick();
        check(f.position() != stop, "World spatial binding did not break physics ancestry");
        a.destination_x = 1000;
        agent.set<NavigationAgent>(a);
        stop = f.position();
        f.tick();
        check(f.position() == stop, "No-path agent moved");
        check(!agent.owns<LocalRotation>() && !agent.owns<LocalScale>(),
              "Movement materialized unrelated TRS channels");
        auto saved = scene.snapshot();
        scene.restore_snapshot(saved);
        check(scene.entity("agent").get<NavigationAgent>().navmesh.id == record.id,
              "Scene round trip lost reference");
        scene.entity("agent").set<LocalRotation>({});
        scene.entity("agent").set<LocalScale>({1, 1, 1});
        auto prefab = create_prefab_source(scene, "agent");
        scene.set_prefab_sources({{prefab.asset(), prefab.source}});
        auto instance = instantiate_prefab(scene, prefab.asset());
        authoring_command(scene, "property.set",
                          {{"entity", instance},
                           {"component", "forge.navigation_agent"},
                           {"field", "speed"},
                           {"value", 2}});
        auto overridden = scene.snapshot();
        check(scene.undo() && scene.redo() && scene.snapshot() == overridden,
              "Navigation override undo/redo failed");
        auto duplicate = authoring_command(scene, "entity.duplicate", {{"entity", instance}})
                             .at("selected")
                             .get<std::string>();
        check(scene.entity(duplicate).get<NavigationAgent>().navmesh.id == record.id,
              "Duplicate lost asset reference");
        auto changed = prefab.source;
        changed["revision"] = 2;
        for (auto& member : changed["members"])
            if (member["id"] == changed["root"])
                member["components"]["forge.navigation_agent"]["speed"] = 3;
        scene.set_prefab_sources({{prefab.asset(), changed}});
        check(scene.entity(instance).get<NavigationAgent>().speed == 2, "Equal override lost");
        authoring_command(
            scene, "property.revert",
            {{"entity", instance}, {"component", "forge.navigation_agent"}, {"field", "speed"}});
        check(scene.entity(instance).get<NavigationAgent>().speed == 3, "Navigation Revert failed");
        Scene reopened(author.world());
        reopened.restore_snapshot(scene.snapshot());
        check(reopened.entity(instance).get<NavigationAgent>() ==
                  scene.entity(instance).get<NavigationAgent>(),
              "Prefab reopen changed configuration");

        authoring_command(scene, "property.set",
                          {{"entity", duplicate},
                           {"component", "forge.local_translation"},
                           {"field", "x"},
                           {"value", -8}});
        Fixture prefab_runtime(root);
        prefab_runtime.scene.restore_snapshot(scene.snapshot());
        prefab_runtime.tick();
        auto realized = prefab_runtime.scene.entity(duplicate);
        check(realized.owns<LocalTranslation>() && !realized.owns<LocalRotation>() &&
                  !realized.owns<LocalScale>(),
              "Prefab navigation did not preserve per-channel inheritance");
        auto runtime_config = realized.get<NavigationAgent>();
        runtime_config.destination_z = 5;
        realized.set<NavigationAgent>(runtime_config);
        auto local = realized.get<LocalTranslation>();
        local.z = 1;
        realized.set<LocalTranslation>(local);
        auto semantic = prefab_runtime.navigation->checkpoint();
        Fixture resumed_prefab(root);
        resumed_prefab.scene.restore_snapshot(prefab_runtime.scene.snapshot());
        resumed_prefab.navigation->restore(semantic);
        auto resumed_agent = resumed_prefab.scene.entity(duplicate);
        check(resumed_agent.get<NavigationAgent>() == runtime_config &&
                  resumed_agent.get<LocalTranslation>() == local,
              "Prefab gameplay semantic recovery lost fields behind property intent");
        check(!resumed_agent.owns<LocalRotation>() && !resumed_agent.owns<LocalScale>(),
              "Recovery created unrelated overrides");
        f.scene.entity("agent").destruct();
        f.tick();
        check(f.navigation->debug(0).is_null(), "Deleted agent survived");
        auto retained = f.navigation;
        retained->shutdown();
        check(retained->find_path({record.id}, {}, {1, 0, 0}).status == NavStatus::Unavailable,
              "Stopped navigation service remained live");
        std::filesystem::remove_all(root);
        std::cout << "Navigation publication, identity, fixed-tick movement, obstacle route, "
                     "recovery and physics ownership passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
