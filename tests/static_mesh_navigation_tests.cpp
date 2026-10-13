#include "asset_bytes.hpp"
#include "editable_mesh_authoring.hpp"
#include "model_authoring.hpp"
#include "model_import_test.hpp"
#include "model_render_resource.hpp"
#include "navigation_geometry.hpp"
#include "runtime_package.hpp"
#include <forge/authoring.hpp>
#include <forge/navigation.hpp>
#include <forge/runtime.hpp>
#include <forge/runtime_resources.hpp>
#include <fstream>
#include <iostream>
#include <thread>
using namespace forge;
using namespace std::chrono_literals;
namespace {
void check(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void reject(F fn) {
    try {
        fn();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid static Mesh navigation operation accepted");
}
Json read(const std::filesystem::path& path) {
    Json value;
    std::ifstream(path) >> value;
    return value;
}
AssetId imported_floor(const std::filesystem::path& root,
                       const std::shared_ptr<ProjectLease>& lease) {
    // Original four-vertex glTF fixture, not an external engine/sample asset.
    const std::array<float, 12> positions{-.5f, .5f, -.5f, -.5f, .5f, .5f,
                                          .5f,  .5f, .5f,  .5f,  .5f, -.5f};
    const std::array<std::uint16_t, 6> indices{0, 1, 2, 0, 2, 3};
    {
        std::ofstream file(root / "Assets/floor.bin", std::ios::binary);
        file.write(reinterpret_cast<const char*>(positions.data()), sizeof positions);
        file.write(reinterpret_cast<const char*>(indices.data()), sizeof indices);
        check(bool(file.flush()), "Original glTF bytes save failed");
    }
    Json gltf = {
        {"asset", {{"version", "2.0"}}},
        {"scene", 0},
        {"scenes", Json::array({{{"nodes", {0}}}})},
        {"nodes", Json::array({{{"name", "Static floor"}, {"mesh", 0}}})},
        {"meshes", Json::array({{{"name", "Floor"},
                                 {"primitives", Json::array({{{"attributes", {{"POSITION", 0}}},
                                                              {"indices", 1}}})}}})},
        {"buffers", Json::array({{{"uri", "floor.bin"}, {"byteLength", 60}}})},
        {"bufferViews", Json::array({{{"buffer", 0}, {"byteOffset", 0}, {"byteLength", 48}},
                                     {{"buffer", 0}, {"byteOffset", 48}, {"byteLength", 12}}})},
        {"accessors",
         Json::array(
             {{{"bufferView", 0},
               {"componentType", 5126},
               {"count", 4},
               {"type", "VEC3"},
               {"min", {-.5, .5, -.5}},
               {"max", {.5, .5, .5}}},
              {{"bufferView", 1}, {"componentType", 5123}, {"count", 6}, {"type", "SCALAR"}}})}};
    atomic_write(root / "Assets/floor.gltf", gltf.dump());
    auto registry = std::make_shared<AssetImporterRegistry>();
    registry->add(std::make_shared<forge::test::DirectModelImporter>(
        asset_detail::model_importer({}), std::filesystem::path{}));
    registry->seal();
    AssetImportService importer(lease, registry, {"windows", "d3d12", "desktop"});
    importer.submit(
        importer.prepare("Assets/floor.gltf"),
        [](auto& candidate, const auto& plan, const auto& catalog) {
            prepare_model_publication(candidate, plan, catalog);
        },
        [](const auto&, const auto& artifact) {
            check(!artifact.files.empty(), "Imported fixture has no cook");
        });
    check(importer.wait_idle(10s), "Original glTF import timed out");
    const auto done = importer.poll();
    check(done.size() == 1 && done[0].published,
          done.empty() ? "No imported fixture receipt" : done[0].diagnostic);
    for (const auto& [id, record] : done[0].publication->catalog.records())
        if (record.type == MeshAsset::type && record.subasset)
            return id;
    throw std::runtime_error("Original glTF did not publish a Mesh member");
}
struct Fixture {
    EngineContext engine;
    Scene scene;
    RuntimeSimulation simulation;
    Fixture(const std::filesystem::path& root, const Json& source)
        : engine(WorldRole::Runtime, false,
                 {runtime_resources_module(root), navigation_module(root)}),
          scene(engine.world()), simulation(engine.world(), scene) {
        scene.restore_snapshot(source);
    }
    std::shared_ptr<NavigationService> navigation() { return engine.services().navigation(); }
    void ready(AssetId nav) {
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        for (;;) {
            engine.services().resources()->synchronize();
            const auto result = navigation()->find_path({nav}, {-8, .1, 0}, {8, .1, 0});
            if (result.status == NavStatus::Success)
                return;
            check(result.status == NavStatus::Unavailable, result.diagnostic);
            check(std::chrono::steady_clock::now() < deadline, "Navigation Mesh load timed out");
            std::this_thread::sleep_for(1ms);
        }
    }
    void journey(EntityRef agent) {
        bool detour = false;
        for (unsigned i = 0; i < 660; ++i) {
            simulation.tick(1.f / 60);
            const auto state = navigation()->agent_state(agent).value();
            check(state.sampled && state.has_path && state.status == NavStatus::Success,
                  "Static Mesh route lost its movement sample");
            check(!(std::abs(state.position[0]) < 1 && std::abs(state.position[2]) < 2),
                  "Static Mesh route entered obstacle footprint");
            if (std::abs(state.position[0]) < .8)
                detour |= std::abs(state.position[2]) > 2;
        }
        check(detour && navigation()->agent_state(agent)->phase == NavigationAgentPhase::Arrived,
              "Static Mesh route did not arrive around the obstacle");
    }
};
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need scratch root and nav worker");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "Assets");
        const auto lease = std::make_shared<ProjectLease>(root);
        const auto mesh_id = AssetId::generate();
        auto source = EditableMeshSource::create_cube(mesh_id);
        const std::filesystem::path source_path = "Assets/Floor.mesh.json";
        AssetCatalog initial(root);
        initial.add({mesh_id, MeshAsset::type, source_path});
        initial.save(AssetCatalog::project_index(root));
        AssetImportService importer(lease, editable_mesh_import_registry(),
                                    {"windows", "d3d12", "desktop"});
        auto publish_mesh = [&] {
            atomic_write(root / source_path, source.document.dump());
            importer.submit(
                importer.prepare(source_path),
                [](auto& c, const auto& p, const auto&) {
                    prepare_editable_mesh_publication(c, p);
                },
                [](const auto&, const auto& artifact) {
                    (void)decode_mesh(artifact.files.at(0).bytes);
                });
            check(importer.wait_idle(10s), "Editable Mesh import timed out");
            const auto done = importer.poll();
            check(done.size() == 1 && done.front().published,
                  done.empty() ? "No Mesh receipt" : done.front().diagnostic);
        };
        publish_mesh();
        EngineContext authored;
        Scene scene(authored.world());
        auto entity = [&](const char* name, LocalTranslation p, LocalScale scale) {
            const auto id = authoring_command(scene, "entity.create", {{"name", name}})
                                .at("selected")
                                .get<std::string>();
            auto e = scene.entity(id);
            e.set<LocalTranslation>(p);
            e.set<LocalScale>(scale);
            e.set<Primitive>({no_primitive});
            e.remove<Tint>();
            return id;
        };
        const auto floor = entity("Cooked floor", {0, -.2, 0}, {20, .4, 20});
        scene.entity(floor).set<MeshRenderer>({{mesh_id}});
        scene.entity(floor).set<NavigationSurface>({true});
        const auto wall = entity("Static wall", {0, 2, 0}, {2, 4, 4});
        scene.entity(wall).set<Primitive>({0});
        scene.entity(wall).set<NavigationSurface>({true});
        const auto agent = entity("Agent", {-8, .1, 0}, {1, 1, 1});
        const auto worker = std::filesystem::absolute(argv[2]);
        auto prepare = [&] {
            return prepare_navigation(root, scene.effective_document(), {}, worker);
        };
        auto nav = prepare().publish(scene.effective_document());
        check(nav.schema_version == 2 && nav.metadata.at("geometry_version") == 2 &&
                  nav.metadata.at("mesh_inputs").size() == 1 && nav.dependency_edges.size() == 2,
              "Mesh navigation lost versioned typed provenance");
        const auto selected =
            asset_detail::selected_mesh_revision(AssetCatalog::open_project(root), {mesh_id});
        check(nav.metadata.at("mesh_inputs")[0].at("revision") == selected &&
                  nav.dependency_edges[1].target == mesh_id &&
                  nav.dependency_edges[1].expected_type == "mesh" &&
                  nav.dependency_edges[1].kind == AssetDependencyKind::Build,
              "Mesh bake recorded a different revision or dependency");
        const auto original_nav = nav.id;
        auto candidate = prepare();
        // Translate all vertices instead: retain planar faces during an actual new cook.
        source = EditableMeshSource::create_cube(mesh_id);
        source.translate_vertices(std::array<std::uint32_t, 8>{1, 2, 3, 4, 5, 6, 7, 8},
                                  {.01, 0, 0});
        publish_mesh();
        const auto after_import = read(AssetCatalog::project_index(root));
        reject([&] { candidate.publish(scene.effective_document()); });
        check(read(AssetCatalog::project_index(root)) == after_import &&
                  after_import.at("assets").size() == 2,
              "Stale navigation candidate changed published selections");
        nav = prepare().publish(scene.effective_document());
        check(nav.id == original_nav, "Rebuild changed NavMesh identity");
        scene.entity(agent).set<NavigationAgent>({{nav.id}, true, true, 2, .1f, 8, .1, 0});
        const EntityRef ref{scene.asset_id(), Json(agent).get<EntityId>()};
        const auto saved = scene.snapshot();
        {
            EngineContext missing(WorldRole::Runtime, false, {navigation_module(root)});
            Scene copy(missing.world());
            copy.restore_snapshot(saved);
            RuntimeSimulation simulation(missing.world(), copy);
            simulation.tick(1.f / 60);
            const auto state = missing.services().navigation()->agent_state(ref).value();
            check(state.phase == NavigationAgentPhase::Failed &&
                      state.status == NavStatus::Unavailable && !state.sampled && !state.has_path,
                  "External Mesh navigation ran without its resource provider");
        }
        {
            Fixture runtime(root, saved);
            runtime.simulation.tick(1.f / 60);
            const auto pending = runtime.navigation()->agent_state(ref).value();
            check(pending.phase == NavigationAgentPhase::Pending && !pending.sampled &&
                      pending.tick == 0,
                  "Unadopted Mesh navigation did not hold a Pending agent");
            runtime.ready(nav.id);
            const auto resources = runtime.engine.services().resources();
            const auto token = resources->request(RuntimeResourceKind::Mesh, mesh_id);
            resources->synchronize();
            const auto retained = resources->mesh(token);
            check(bool(retained), "Navigation and another consumer did not share a ready mesh");
            // After adoption, no cook/source files are needed by repeated navigation ticks.
            const auto hidden = root.string() + "-cache-hidden";
            const auto cache = root / ".forge/cache";
            check(std::filesystem::exists(cache), "Expected immutable cook cache is absent");
            std::filesystem::rename(cache, hidden);
            std::filesystem::remove(root / source_path);
            runtime.journey(ref);
            check(&resources->mesh(token).get() == &retained.get(),
                  "Navigation replaced unchanged CPU data");
            if (std::filesystem::exists(hidden))
                std::filesystem::rename(hidden, cache);
            atomic_write(root / source_path, source.document.dump());
            auto catalog = AssetCatalog::open_project(root);
            auto bad = catalog.records().at(mesh_id);
            bad.metadata["forge.import"]["key"] = std::string(64, 'f');
            bad.metadata["forge.import"]["generation"] = std::uint64_t(3);
            catalog.replace(bad);
            catalog.save(AssetCatalog::project_index(root));
            resources->refresh();
            const auto deadline = std::chrono::steady_clock::now() + 10s;
            while (resources->inspect(token).state != "failed") {
                resources->synchronize();
                check(std::chrono::steady_clock::now() < deadline, "Replacement did not fail");
                std::this_thread::sleep_for(1ms);
            }
            check(resources->mesh(token) && &resources->mesh(token).get() == &retained.get() &&
                      resources->inspect(token).requested_revision != retained.identity().revision,
                  "Failed Mesh replacement lost or relabeled last-good CPU data");
            check(runtime.navigation()->find_path({nav.id}, {-8, .1, 0}, {8, .1, 0}).status ==
                      NavStatus::Stale,
                  "Navigation followed retained data after selected Mesh changed");
            catalog = AssetCatalog(root);
            catalog.restore(after_import);
            // Restore the final selected navigation record, not the discarded candidate.
            catalog.replace(nav);
            catalog.save(AssetCatalog::project_index(root));
        }
        const std::filesystem::path scene_path = "Scenes/static.scene.json";
        std::filesystem::create_directories(root / "Scenes");
        atomic_write(root / scene_path, scene.document().dump());
        auto catalog = AssetCatalog::open_project(root);
        catalog.add_scene(scene_path);
        catalog.save(AssetCatalog::project_index(root));
        const auto game = root.string() + "-game";
        const std::array roots{scene.asset_id()};
        (void)package_runtime_content(root, game, roots, {"windows", "d3d12"});
        const auto moved = game + "-moved", hidden = root.string() + "-offline";
        std::filesystem::rename(game, moved);
        std::filesystem::rename(root, hidden);
        const auto shipped = open_runtime_content(moved, {"windows", "d3d12"});
        check(shipped.records().size() == 3 &&
                  shipped.records().at(nav.id).dependency_edges.empty() &&
                  !std::filesystem::exists(std::filesystem::path(moved) / source_path),
              "Mesh navigation export lost closure or retained authored Mesh source");
        const auto exported =
            read(std::filesystem::path(moved) / shipped.records().at(scene.asset_id()).source);
        {
            Fixture runtime(moved, exported);
            runtime.ready(nav.id);
            runtime.journey(ref);
        }
        std::filesystem::rename(hidden, root);
        std::filesystem::remove_all(moved);
        const auto imported = imported_floor(root, lease);
        auto renderer = scene.entity(floor).get<MeshRenderer>();
        renderer.mesh = {imported};
        scene.entity(floor).set<MeshRenderer>(renderer);
        nav = prepare().publish(scene.effective_document());
        check(
            nav.id == original_nav &&
                nav.metadata.at("mesh_inputs")[0].at("asset_id") == Json(imported),
            "Imported Mesh bake changed identity or recorded the root Model instead of typed Mesh");
        {
            Fixture runtime(root, scene.snapshot());
            runtime.ready(nav.id);
            runtime.journey(ref);
        }
        atomic_write(root / scene_path, scene.document().dump());
        (void)package_runtime_content(root, game, roots, {"windows", "d3d12"});
        std::filesystem::rename(game, moved);
        std::filesystem::rename(root, hidden);
        const auto imported_game = open_runtime_content(moved, {"windows", "d3d12"});
        check(imported_game.records().contains(imported),
              "Imported Mesh missing from exported scene closure");
        {
            Fixture runtime(moved, read(std::filesystem::path(moved) /
                                        imported_game.records().at(scene.asset_id()).source));
            runtime.ready(nav.id);
            runtime.journey(ref);
        }
        std::filesystem::remove_all(hidden);
        std::filesystem::remove_all(moved);
        std::cout << "Static Mesh navigation publication/lifetimes/route/offline export passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
