#include "collision_authoring.hpp"
#include "collision_bundle.hpp"
#include "collision_document.hpp"
#include "collision_selection.hpp"
#include "physics_debug.hpp"
#include "runtime_package.hpp"
#include <forge/engine_assets.hpp>
#include <forge/game_session.hpp>
#include <fstream>
#include <iostream>
#include <thread>

using namespace forge;
using namespace std::chrono_literals;
namespace {
void require(bool v, const std::string& why) {
    if (!v)
        throw std::runtime_error(why);
}
void save(const std::filesystem::path& path, const CollisionSource& source) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << source.document.dump();
    require(bool(out.flush()), "Collision fixture write failed");
}
AssetImportOutcome import(AssetImportService& service, const CollisionSource& source) {
    service.submit(
        service.prepare("Assets/test.collision.json", {}, source.asset()),
        [](auto& candidate, const auto& plan, const auto&) {
            prepare_collision_publication(candidate, plan);
        },
        [](const auto&, const auto& artifact) {
            (void)collision_detail::decode_bundle(artifact.files);
        });
    require(service.wait_idle(15s), "Collision import timed out");
    auto completed = service.poll();
    require(completed.size() == 1, "Collision import did not return one receipt");
    return std::move(completed[0]);
}
void wait_physics(PhysicsRuntime& physics) {
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (!physics.prepare_assets()) {
        require(std::chrono::steady_clock::now() < deadline,
                "Physics collision preparation timed out");
        std::this_thread::sleep_for(1ms);
    }
}
void runtime_collision(const std::filesystem::path& root, AssetId asset,
                       const std::function<void(GameSession&)>& replacement = {}) {
    GameSessionConfig config;
    config.content_root = root;
    GameSession game(config);
    Json scene{{"version", 1},
               {"entities", Json::array({{{"id", "collision"},
                                          {"name", "Collision"},
                                          {"components",
                                           {{"forge.position", {{"x", 0}, {"y", 0}, {"z", 0}}},
                                            {"forge.physics_body",
                                             {{"motion", 0},
                                              {"density", 500},
                                              {"mass", 0},
                                              {"friction", .5},
                                              {"restitution", 0},
                                              {"gravity_factor", 1}}},
                                            {"forge.asset_collider", {{"asset", asset}}}}}}})}};
    const auto ticket = game.prepare(scene);
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (!game.poll_preparation(ticket).ready) {
        require(std::chrono::steady_clock::now() < deadline, "Scene collision readiness timed out");
        std::this_thread::sleep_for(1ms);
    }
    game.activate(ticket, RuntimeClock::Time{}, false);
    auto physics = game.active().physics();
    require(physics->raycast({0, 0, -5}, {0, 0, 10}).has_value(),
            "Asset collider has no native collision");
    const auto checkpoint = physics->checkpoint();
    Module module;
    RuntimeWorld recovery(module, {}, {}, {}, root, false);
    recovery.scene.restore_snapshot(game.active().scene.snapshot());
    wait_physics(*recovery.physics());
    recovery.physics()->restore(checkpoint);
    require(recovery.physics()->raycast({0, 0, -5}, {0, 0, 10}).has_value(),
            "Asset collider recovery lost geometry");
    if (replacement)
        replacement(game);
    // Missing resources reject only the unpublished candidate, preserving old world.
    scene["entities"][0]["components"]["forge.asset_collider"]["asset"] = AssetId::generate();
    bool rejected = false;
    try {
        auto failed = game.prepare(scene);
        while (!game.poll_preparation(failed).ready) {
            require(std::chrono::steady_clock::now() < deadline,
                    "Invalid candidate did not finish");
            std::this_thread::sleep_for(1ms);
        }
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected && game.active().physics() == physics &&
                physics->raycast({0, 0, -5}, {0, 0, 10}).has_value(),
            "Failed collision load replaced active scene");
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Need collision scratch directory");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "Assets");
        auto lease = std::make_shared<ProjectLease>(root);
        bool creation_rejected = false;
        try {
            CollisionDocument::create(lease, "Assets/rejected.collision.json", [](AssetId id) {
                auto source = CollisionSource::create(id, CollisionKind::Box);
                source.document["nodes"][0]["scale"] = {0, 1, 1};
                return source;
            });
        } catch (const std::exception&) {
            creation_rejected = true;
        }
        require(creation_rejected &&
                    !std::filesystem::exists(root / "Assets/rejected.collision.json"),
                "Invalid initial collision recipe left a source file behind");
        auto authored = CollisionDocument::create(lease, "Assets/authored.collision.json");
        const auto initial = authored->source().document;
        authored->edit(authored->revision(), "Move shape",
                       [](Json& d) { d["nodes"][0]["translation"] = {1, 0, 0}; });
        require(authored->dirty(), "Collision document edit was not dirty");
        authored->undo();
        require(authored->source().document == initial, "Collision document Undo failed");
        authored->redo();
        authored->save();
        CollisionDocument reopened(lease, "Assets/authored.collision.json");
        require(reopened.source().document == authored->source().document && !reopened.dirty(),
                "Collision source save/reopen failed");
        auto source = CollisionSource::from_mesh(AssetId::generate(), CollisionKind::ConvexHull,
                                                 engine_primitive(0));
        auto& node = source.document["nodes"][0];
        const auto path = root / "Assets/test.collision.json";
        save(path, source);
        AssetImportService service(lease, collision_import_registry(), {"linux", "none", "cpu"});
        auto result = import(service, source);
        require(result.published, "Collision source failed publication: " + result.diagnostic);
        auto catalog = AssetCatalog::open_project(root);
        const auto selected = catalog.document();
        const auto& edges = catalog.dependency_graph().dependencies(source.asset());
        require(edges.size() == 1 && edges[0].kind == AssetDependencyKind::Build &&
                    edges[0].target == engine_primitive(0).id && !edges[0].revision.empty(),
                "Collision source was not recorded as a Build-only revision dependency");
        ResourcePool<CollisionAsset> pool;
        auto ticket = request_collision(pool, root, std::make_shared<const AssetCatalog>(catalog),
                                        {source.asset()});
        require(pool.wait(ticket, 10s),
                "Published collision failed runtime preparation: " + ticket.inspect().diagnostic);
        auto original = pool.acquire(ticket);
        require(bool(original), "Published collision resource has no lease");
        {
            Module module;
            RuntimeWorld runtime(module, {}, PhysicsConfig{}, std::nullopt, root, false);
            const auto resources = runtime.engine.services().resources();
            const auto observer =
                resources->request(RuntimeResourceKind::Collision, source.asset());
            const auto bad = runtime.physics()->request_collision_asset(AssetId::generate());
            const auto deadline = std::chrono::steady_clock::now() + 10s;
            while (resources->inspect(observer).state != "ready") {
                require(std::chrono::steady_clock::now() < deadline,
                        "Shared collision preload did not finish");
                runtime.physics()->synchronize(1.f / 60);
                std::this_thread::sleep_for(1ms);
            }
            require(!resources->inspect(observer).retained_revision.empty(),
                    "Collision preload did not retain native revision");
            require(runtime.physics()->inspect_collision_asset(bad).state == "failed",
                    "Invalid optional preload was not reported");
            require(resources->release(observer) && !resources->release(observer),
                    "Collision subscription release failed");
            runtime.physics()->release_collision_asset(bad);
        }
        {
            Json components = {{"forge.physics_body",
                                {{"motion", 0},
                                 {"density", 1000},
                                 {"mass", 0},
                                 {"friction", .5},
                                 {"restitution", 0},
                                 {"gravity_factor", 1},
                                 {"enabled", true},
                                 {"sensor", false},
                                 {"layer", 0},
                                 {"mask", UINT32_MAX}}},
                               {"forge.asset_collider", {{"asset", source.asset()}}}};
            const auto mesh = prepare_physics_debug(components, {-2, 1, 1}, original);
            require(!mesh.triangles.empty(), "Native convex preview missing");
            float low = 0, high = 0;
            for (const auto& tri : mesh.triangles)
                for (const auto& p : tri) {
                    low = std::min(low, p[0]);
                    high = std::max(high, p[0]);
                }
            require(low < -.9 && high > .9, "Native mirrored hull preview has wrong scale/bounds");
        }
        // Unknown source UUID references are never scanned or rewritten.
        source.document["extension"] = {{"opaque", AssetId::generate().str()}};
        node["scale"] = {0, 1, 1};
        save(path, source);
        result = import(service, source);
        require(!result.published && AssetCatalog::open_project(root).document() == selected &&
                    pool.current({source.asset()}).identity() == original.identity(),
                "Rejected collision candidate damaged selected catalog/resource");
        node["scale"] = {-2, 1, 1};
        save(path, source);
        result = import(service, source);
        require(result.published, "Valid collision replacement failed: " + result.diagnostic);
        catalog = AssetCatalog::open_project(root);
        ticket = request_collision(pool, root, std::make_shared<const AssetCatalog>(catalog),
                                   {source.asset()});
        require(pool.wait(ticket, 10s) && pool.acquire(ticket).identity() != original.identity(),
                "New collision revision failed replacement");
        runtime_collision(root, source.asset(), [&](GameSession& game) {
            auto physics = game.active().physics();
            const auto before = physics->checkpoint();
            const auto old_hit = physics->raycast({-5, 0, 0}, {10, 0, 0});
            node["scale"] = {-3, 1, 1};
            save(path, source);
            const auto updated = import(service, source);
            require(updated.published, "Live collision update failed publication");
            const auto key = AssetCatalog::open_project(root)
                                 .records()
                                 .at(source.asset())
                                 .metadata.at("forge.import")
                                 .at("key");
            physics->refresh_assets();
            for (unsigned i = 0; i < 30; ++i) {
                physics->prepare_assets();
                std::this_thread::sleep_for(1ms);
            }
            require(physics->checkpoint() == before,
                    "Paused resource polling changed realized collision/recovery");
            const auto deadline = std::chrono::steady_clock::now() + 10s;
            while (physics->status()["collision_resources"][0]["revision"] != key) {
                physics->synchronize(0);
                require(std::chrono::steady_clock::now() < deadline,
                        "Collision boundary adoption timed out");
                std::this_thread::sleep_for(1ms);
            }
            const auto hit = physics->raycast({-5, 0, 0}, {10, 0, 0});
            require(hit && old_hit && hit->fraction < old_hit->fraction,
                    "Live body kept old geometry after boundary");
        });
        catalog = AssetCatalog::open_project(root);
        // Runtime load is cooked-only: deleting the authored recipe does not
        // remove the selected artifact or force a source import.
        std::filesystem::remove(path);
        ResourcePool<CollisionAsset> fresh;
        auto source_free = request_collision(
            fresh, root, std::make_shared<const AssetCatalog>(catalog), {source.asset()});
        require(fresh.wait(source_free, 10s) && bool(fresh.acquire(source_free)),
                "Collision runtime unexpectedly depended on the source recipe");
        runtime_collision(root, source.asset());
        const auto packaged = root.parent_path() / (source.asset().str() + "-package");
        const std::array roots{source.asset()};
        package_runtime_content(root, packaged, roots, {"linux", "none"});
        const auto relocated = root.parent_path() / (source.asset().str() + "-relocated");
        std::filesystem::rename(packaged, relocated);
        const auto hidden = root.string() + "-unavailable";
        std::filesystem::rename(root, hidden);
        runtime_collision(relocated, source.asset());
        std::cout << "Collision publication/resource pipeline passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
