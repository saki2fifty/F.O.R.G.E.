#include "collision_authoring.hpp"
#include "collision_bundle.hpp"
#include "collision_document.hpp"
#include "collision_selection.hpp"
#include "editor/document.hpp"
#include "navigation_asset.hpp"
#include <chrono>
#include <forge/authoring.hpp>
#include <forge/game_session.hpp>
#include <forge/navigation.hpp>
#include <forge/physics.hpp>
#include <forge/physics_components.hpp>
#include <forge/runtime_resource_service.hpp>
#include <iostream>
#include <thread>

using namespace forge;
using namespace std::chrono_literals;
namespace {
void require(bool value, const std::string& reason) {
    if (!value)
        throw std::runtime_error(reason);
}
void publish(AssetImportService& imports, const CollisionSource& source) {
    imports.submit(
        imports.prepare("Assets/Floor.collision.json", {}, source.asset()),
        [](auto& candidate, const auto& plan, const auto&) {
            prepare_collision_publication(candidate, plan);
        },
        [](const auto&, const auto& artifact) {
            (void)collision_detail::decode_bundle(artifact.files);
        });
    require(imports.wait_idle(15s), "Collision publication timed out");
    const auto outcomes = imports.poll();
    require(outcomes.size() == 1 && outcomes[0].published,
            "Collision publication failed: " +
                (outcomes.empty() ? std::string("missing receipt") : outcomes[0].diagnostic));
}
void exercise(const std::filesystem::path& root, const std::filesystem::path& worker,
              bool dynamic) {
    using namespace navigation_detail;
    SceneDocument::create_project(root, "Cooked physical navigation");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root);
    auto source = CollisionDocument::create(
        project.writer_guard(), "Assets/Floor.collision.json", [](AssetId id) {
            auto result = CollisionSource::create(id, CollisionKind::Box);
            result.document["nodes"][0]["dimensions"] = {12, 1, 12};
            return result;
        });
    AssetImportService imports(project.writer_guard(), collision_import_registry(),
                               collision_import_target());
    publish(imports, source->source());
    const auto asset = source->source().asset();
    const auto selected = selected_collision_revision(AssetCatalog::open_project(root), {asset});
    const auto add = [&](const char* name) {
        return authoring_command(scene, "entity.create", {{"name", name}, {"kind", 4}})
            .at("selected")
            .get<std::string>();
    };
    const auto floor_id = add("Unmarked Collision asset floor");
    scene.entity(floor_id)
        .set<LocalTranslation>({0, -.5, 0})
        .set<PhysicsBody>({})
        .set<AssetCollider>({{asset}});
    NavigationSettings settings;
    settings.collect_static_colliders = true;
    settings.dynamic_obstacles = dynamic;
    settings.profile = "CookedFloor";
    settings.radius = .2f;
    project.save();
    const auto document = scene.effective_document();
    const auto nav = prepare_navigation(root, document, settings, worker).publish(document);
    require(nav.metadata.at("collision_inputs") ==
                Json::array({{{"asset_id", asset}, {"revision", selected}}}),
            "Navigation did not bind the actual selected Collision revision");
    const auto edge = std::find_if(
        nav.dependency_edges.begin(), nav.dependency_edges.end(), [&](const auto& value) {
            return value.target == asset && value.kind == AssetDependencyKind::Build &&
                   value.expected_type == CollisionAsset::type &&
                   value.role == "navigation.collision" && value.revision == selected;
        });
    require(edge != nav.dependency_edges.end(), "Collision provenance lost its normal Build edge");
    const auto admitted = load(root, nav);
    Query query(admitted.mesh);
    const auto route = query.path({-4, 0, 0}, {4, 0, 0});
    require(route.status == NavStatus::Success, "Cooked physical floor did not produce a route");
    const auto start = route.points.front(), end = route.points.back();
    const auto actor_id = add("Cooked physical route follower");
    NavigationAgent agent;
    agent.navmesh = {nav.id};
    agent.speed = 3;
    agent.has_destination = true;
    agent.destination_x = end[0];
    agent.destination_y = end[1];
    agent.destination_z = end[2];
    scene.entity(actor_id).set<LocalTranslation>({start[0], start[1], start[2]}).set(agent);
    project.save();
    GameSessionConfig config;
    config.content_root = root;
    GameSession session(config);
    const auto candidate = session.prepare(scene.snapshot());
    const auto deadline = std::chrono::steady_clock::now() + 15s;
    while (!session.poll_preparation(candidate).ready) {
        require(std::chrono::steady_clock::now() < deadline, "Cooked source readiness timed out");
        std::this_thread::sleep_for(1ms);
    }
    session.activate(candidate, {}, false);
    auto& world = session.active();
    const auto navigation = world.engine.services().navigation();
    const auto resources = world.engine.services().resources();
    const auto actor = world.scene.entity(actor_id);
    require(navigation->find_path({nav.id}, start, end).status == NavStatus::Success,
            "Navigation did not share current Collision resource readiness with physics");
    for (unsigned step = 0; step < 30; ++step)
        session.step();
    const auto position = actor.get<LocalTranslation>();
    require(position.x > start[0] + .5, "Cooked resource Agent did not move");
    auto paused = actor.get<NavigationAgent>();
    paused.speed = 0;
    actor.set(paused);
    source->edit(source->revision(), "Resize floor",
                 [](Json& value) { value["nodes"][0]["dimensions"] = {14, 1, 12}; });
    source->save();
    publish(imports, source->source());
    const auto replaced = selected_collision_revision(AssetCatalog::open_project(root), {asset});
    require(replaced != selected && source->source().asset() == asset,
            "Collision replacement changed identity or failed to select new bytes");
    require(navigation_geometry_digest(root, scene.effective_document(), nav.metadata) !=
                nav.metadata.at("geometry_sha256"),
            "Selected Collision replacement did not invalidate editor/source provenance");
    // Collision selections follow the existing physics refresh/adoption owner;
    // generic resource refresh deliberately does not replace its catalog boundary.
    std::static_pointer_cast<PhysicsRuntime>(world.engine.services().physics())->refresh_assets();
    resources->refresh();
    const auto watch = resources->request(RuntimeResourceKind::Collision, asset);
    const auto adoption_deadline = std::chrono::steady_clock::now() + 10s;
    for (;;) {
        session.step();
        const auto status = resources->inspect(watch);
        if (status.state == "ready" && status.requested_revision == replaced &&
            status.retained_revision == replaced)
            break;
        require(std::chrono::steady_clock::now() < adoption_deadline,
                "Shared native Collision subscription did not adopt replacement: " +
                    status.diagnostic);
        std::this_thread::sleep_for(1ms);
    }
    session.step();
    require(navigation->find_path({nav.id}, start, end).status != NavStatus::Success,
            "Navigation accepted an old mesh against the new Collision resource revision");
    paused.speed = 3;
    actor.set(paused);
    for (unsigned step = 0; step < 3; ++step) {
        session.step();
        require(actor.get<LocalTranslation>() == position,
                "Agent moved on stale cooked Collision geometry");
    }
    require(resources->release(watch), "Collision observation subscription did not release");
    session.unload({});
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Need cooked collection scratch directory and native worker");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        for (bool dynamic : {false, true})
            exercise(root / (dynamic ? "dynamic" : "static"), std::filesystem::absolute(argv[2]),
                     dynamic);
        std::filesystem::remove_all(root);
        std::cout << "Collision publication/leases, real physics/resource/navigation readiness, "
                     "stable-ID replacement and stale movement rejection pass\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
