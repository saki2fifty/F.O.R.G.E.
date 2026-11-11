#include "collision_authoring.hpp"
#include "collision_document.hpp"
#include "collision_selection.hpp"
#include "runtime_package.hpp"
#include <algorithm>
#include <cmath>
#include <forge/authoring.hpp>
#include <forge/game_session.hpp>
#include <forge/navigation.hpp>
#include <forge/navigation_build.hpp>
#include <forge/physics_components.hpp>
#include <forge/render_components.hpp>
#include <fstream>
#include <iostream>
#include <memory>
#include <string_view>
#include <thread>

using namespace forge;
namespace {
constexpr const char* scene_path = "Scenes/141-navigation-collision-collection.scene.json";
constexpr const char* collision_path = "Assets/Physics/NavigationWall.collision.json";
constexpr const char* actor_name = "Cyan patrol - collected physical geometry";
// The curated Gallery targets the Windows package. This CPU importer supports
// cross-target preparation; native admission is still checked before reuse.
const ImportTarget gallery_collision_target{"windows", "none", "cpu"};
void check(bool value, const std::string& reason) {
    if (!value)
        throw std::runtime_error(reason);
}
Json read(const std::filesystem::path& path) {
    Json value;
    std::ifstream(path) >> value;
    return value;
}
flecs::entity create(Scene& scene, const char* name, Double3 position = {}, unsigned kind = 4) {
    const auto id = authoring_command(scene, "entity.create", {{"name", name}, {"kind", kind}})
                        .at("selected")
                        .get<std::string>();
    const auto entity = scene.entity(id);
    entity.set<LocalTranslation>({position[0], position[1], position[2]});
    return entity;
}
AssetId prepare_wall(const std::filesystem::path& root, std::shared_ptr<const ProjectLease> lease) {
    std::filesystem::create_directories((root / collision_path).parent_path());
    auto source = std::filesystem::exists(root / collision_path)
                      ? std::make_unique<CollisionDocument>(lease, collision_path)
                      : CollisionDocument::create(lease, collision_path);
    const auto id = source->source().asset();
    AssetImportService imports(lease, collision_import_registry(), gallery_collision_target);
    auto draft = imports.prepare(collision_path, {}, id);
    const auto plan = draft.importer->discover(draft.request, {});
    const auto catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
    try {
        const auto& selected = catalog->records().at(id).metadata.at("forge.import");
        check(selected.at("key") == plan.input.key(), "Collision recipe or native target changed");
        ResourcePool<CollisionAsset> pool;
        const auto ticket = request_collision(pool, root, catalog, {id});
        check(pool.wait(ticket, std::chrono::seconds(15)) && bool(pool.acquire(ticket)),
              "Selected physical shape is unavailable");
        return id;
    } catch (const std::exception&) {
        // The normal publication owner can repair a missing/stale selection.
    }
    imports.submit(
        std::move(draft),
        [](auto& candidate, const auto& plan, const auto&) {
            prepare_collision_publication(candidate, plan);
        },
        [](const auto&, const auto&) {});
    check(imports.wait_idle(std::chrono::seconds(15)), "Gallery collision publication timed out");
    const auto outcomes = imports.poll();
    check(outcomes.size() == 1 && outcomes[0].published,
          "Gallery Collision publication failed" +
              (outcomes.empty() ? std::string{} : ": " + outcomes[0].diagnostic));
    return id;
}
void prepare(const std::filesystem::path& root, const std::filesystem::path& worker) {
    const auto lease = std::make_shared<ProjectLease>(root);
    const auto wall = prepare_wall(root, lease);
    const auto initial = AssetCatalog::open_project(root);
    AssetId patrol;
    for (const auto& [id, record] : initial.records())
        if (record.type == NavigationBehaviorAsset::type &&
            record.source == "Assets/Navigation/Patrol.behavior.json")
            patrol = id;
    check(bool(patrol), "The existing original Patrol behavior is missing");
    EngineContext engine;
    Scene scene(engine.world());
    if (std::filesystem::exists(root / scene_path)) {
        scene.load(root / scene_path);
    } else {
        create(scene, "Physical floor - Static Box, no navigation marker", {0, -.5, 0}, 0)
            .set<LocalScale>({16, 1, 12})
            .set<Tint>({.07f, .15f, .19f})
            .set<PhysicsBody>({})
            .set<BoxCollider>({1, 1, 1});
        create(scene, "Physical wall - Collision asset, no navigation marker", {0, 1, 0}, 0)
            .set<LocalScale>({2, 2, 8})
            .set<Tint>({.8f, .34f, .07f})
            .set<PhysicsBody>({})
            .set<AssetCollider>({{wall}});
        for (double x : {-4., 4.})
            create(scene, "Patrol destination marker", {x, .04, 0}, 0)
                .set<LocalScale>({.8f, .08f, .8f})
                .set<Tint>({.12f, .9f, .4f});
        const auto actor = create(scene, actor_name, {-4, .1, 0});
        actor.set<NavigationAgent>({{}, true, false, 3, .08f, 4, 0, 0})
            .set<NavigationBehavior>({{patrol}, {}, {}, true});
        const auto body = create(scene, "Cyan visible body", {0, .2, 0}, 0);
        body.set<LocalScale>({.6f, .6f, .6f}).set<Tint>({.04f, .8f, 1.f});
        scene.reparent_entity(body.get<PersistentEntityId>().value.str(),
                              actor.get<PersistentEntityId>().value.str(), ReparentMode::KeepLocal);
        Camera camera;
        camera.projection = unsigned(CameraProjection::Orthographic);
        camera.orthographic_height = 18;
        camera.aspect = 16. / 9;
        create(scene, "Overview Camera", {13, 13, -16})
            .set<Camera>(camera)
            .set<LocalRotation>(rotation_from_euler({32.3, -39.1, 0}));
        Light sun;
        sun.cast_shadows = true;
        create(scene, "Sunlight")
            .set<Light>(sun)
            .set<LocalRotation>(rotation_from_euler({55, -25, 0}));
        // Persist identities before publication so preparation can resume safely.
        scene.save(root / scene_path);
    }
    NavigationSettings settings;
    settings.collect_static_colliders = true;
    settings.collision_layers = 1;
    settings.profile = "Physical";
    const auto document = scene.effective_document();
    const auto catalog = AssetCatalog::open_project(root);
    const auto profiles = navigation_profiles(catalog, scene.asset_id());
    AssetId nav;
    if (const auto at = profiles.find(settings.profile); at != profiles.end()) {
        const auto& current = catalog.records().at(at->second);
        if (current.metadata.at("settings") == Json(settings) &&
            Json(navigation_geometry_digest(root, document, current.metadata)) ==
                current.metadata.at("geometry_sha256") &&
            catalog.resolve(AssetRef<NavMeshAsset>{current.id}).state == AssetState::Available)
            nav = current.id;
    }
    if (!nav)
        nav = prepare_navigation(root, document, settings, worker).publish(document).id;
    const auto authored = scene.document();
    for (const auto& row : authored.at("entities"))
        if (row.at("name") == actor_name) {
            const auto entity = scene.entity(row.at("id"));
            auto value = entity.get<NavigationAgent>();
            value.navmesh = {nav};
            entity.set(value);
        }
    scene.save(root / scene_path);
    const std::array roots{scene.asset_id()};
    prepare_runtime_content_catalog(*lease, roots, {}, scene.schema());
    std::cout << "Gallery141 physical collection prepared through normal owners\n";
}
void exercise(const std::filesystem::path& root, const std::filesystem::path& path) {
    const auto saved = read(root / path);
    GameSessionConfig config;
    config.content_root = root;
    struct Preparation final : GameScenePreparation {
        GamePreparationProgress poll(RuntimeWorld& world) override {
            return {world.prepare_scene_resources(), "scene resources"};
        }
        void activate() noexcept override {}
    };
    config.preparation = [](std::uint64_t) { return std::make_unique<Preparation>(); };
    GameSession session(config);
    const auto candidate = session.prepare(saved);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!session.poll_preparation(candidate).ready) {
        check(std::chrono::steady_clock::now() < deadline, "Physical Gallery readiness timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    session.activate(candidate, {}, false);
    auto& world = session.active();
    flecs::entity actor;
    for (const auto& row : saved.at("entities"))
        if (row.at("name") == actor_name)
            actor = world.scene.entity(row.at("id"));
    check(bool(actor), "Collected physical Gallery traveler is missing");
    const auto navigation =
        std::static_pointer_cast<NavigationRuntime>(world.engine.services().navigation());
    bool forward = false, reverse = false;
    double low = 100, high = -100;
    for (unsigned tick = 0; tick < 1200; ++tick) {
        const auto previous = actor.get<LocalTranslation>();
        session.step();
        const auto position = actor.get<LocalTranslation>();
        low = std::min(low, position.x);
        high = std::max(high, position.x);
        if (std::abs(position.x) < .5) {
            check(std::abs(position.z) > 4, "Patrol passed through the collected physical wall");
            forward |= position.x > previous.x;
            reverse |= position.x < previous.x;
        }
        const auto state = navigation->agent_state(
            world.scene.reference(actor.get<PersistentEntityId>().value.str()));
        check(state && state->phase != NavigationAgentPhase::Failed,
              "Physical Gallery patrol failed: " + navigation->debug(actor.id()).dump());
    }
    check(forward && reverse && low < -3.8 && high > 3.8,
          "Gallery did not demonstrate repeated collected-geometry patrol");
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 4 && std::string_view(argv[1]) == "--prepare") {
            prepare(std::filesystem::absolute(argv[2]), std::filesystem::absolute(argv[3]));
            return 0;
        }
        check(argc == 3, "Need Gallery and physical collection scratch root");
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto root = scratch / "Gallery";
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        exercise(root, scene_path);
        const auto saved = read(root / scene_path);
        const std::array roots{saved.at("asset_id").get<AssetId>()};
        const RuntimePackageTarget target{gallery_collision_target.platform, "d3d12"};
        package_runtime_content(root, scratch / "Export", roots, target);
        std::filesystem::rename(scratch / "Export", scratch / "Relocated");
        std::filesystem::rename(root, scratch / "SourcesOffline");
        const auto catalog = open_runtime_content(scratch / "Relocated", target);
        exercise(scratch / "Relocated", catalog.records().at(roots[0]).source);
        std::cout << "Gallery141 physical patrol and source-hidden relocated export pass\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
