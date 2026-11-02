#include "navigation_asset.hpp"
#include "navigation_sources.hpp"
#include "runtime_package.hpp"
#include "runtime_tile_map.hpp"
#include "tile_asset_resources.hpp"
#include <forge/authoring.hpp>
#include <forge/game_session.hpp>
#include <forge/navigation.hpp>
#include <forge/navigation_build.hpp>
#include <forge/physics_components.hpp>
#include <forge/render_components.hpp>
#include <forge/runtime.hpp>
#include <fstream>
#include <iostream>
#include <thread>
using namespace forge;
using namespace forge::navigation_detail;
namespace {
void check(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void rejects(F&& action) {
    bool failed = false;
    try {
        action();
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed, "Unsupported Tile Map navigation input was admitted");
}
void runtime(const std::filesystem::path& root, const Json& saved, AssetId nav_id, EntityRef source,
             EntityRef actor, bool obstacle) {
    GameSessionConfig config;
    config.content_root = root;
    GameSession session(config);
    const auto candidate = session.prepare(saved);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!session.poll_preparation(candidate).ready) {
        check(std::chrono::steady_clock::now() < deadline, "Map navigation preparation timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    session.activate(candidate, {}, false);
    auto& world = session.active();
    auto& context = world.engine.world();
    const auto navigation = world.engine.services().navigation();
    const auto entity = world.scene.entity(actor.entity.str());
    session.step();
    const auto observed = navigation->agent_state(actor);
    check(observed && observed->phase == NavigationAgentPhase::Moving,
          "Prepared Map source did not allow ordinary GameSession movement: " +
              std::static_pointer_cast<NavigationRuntime>(navigation)->debug(entity.id()).dump());
    const auto initial = entity.get<LocalTranslation>();
    const auto map = detail::runtime_tile_map_snapshot(context, source);
    check(map && !map->edited, "Map source has no immutable runtime binding");
    const std::array<TileMapCellEdit, 1> cells{{{1, {0, -4}, {}}}};
    const auto edit = detail::request_runtime_tile_cells(context, "project.navigation_test", source,
                                                         map->generation, cells);
    check(detail::inspect_runtime_tile_edit(context, "project.navigation_test", edit).state ==
              detail::RuntimeTileEditState::Pending,
          "Map cell edit published outside a fixed boundary");
    session.step();
    check(detail::inspect_runtime_tile_edit(context, "project.navigation_test", edit).state ==
                  detail::RuntimeTileEditState::Applied &&
              navigation->find_path({nav_id}, {0, -8, 0}, {0, 8, 0}).status == NavStatus::Stale,
          "Edited Map source did not invalidate the baked mesh");
    const auto stopped = entity.get<LocalTranslation>();
    for (unsigned i = 0; i < 3; ++i)
        session.step();
    const auto held = entity.get<LocalTranslation>();
    check(held.x == stopped.x && held.y == stopped.y && held.z == stopped.z,
          "Agent continued moving along runtime-edited collision geometry");
    const auto reset = detail::request_runtime_tile_reset(context, "project.navigation_test",
                                                          source, map->generation);
    session.step();
    check(detail::inspect_runtime_tile_edit(context, "project.navigation_test", reset).state ==
                  detail::RuntimeTileEditState::Applied &&
              !detail::runtime_tile_map_snapshot(context, source)->edited &&
              navigation->agent_state(actor)->phase == NavigationAgentPhase::Moving,
          "Reset did not resume the unchanged baked Map route");
    check(detail::release_runtime_tile_edit(context, "project.navigation_test", edit) &&
              detail::release_runtime_tile_edit(context, "project.navigation_test", reset),
          "Map source test lost edit-token ownership");
    {
        const auto tile_checkpoint = detail::checkpoint_runtime_tile_maps(context);
        const auto nav_checkpoint =
            std::static_pointer_cast<NavigationRuntime>(navigation)->checkpoint();
        RuntimeWorld recovered({}, {}, std::nullopt, root, false);
        recovered.scene.restore_snapshot(world.scene.snapshot());
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (!recovered.prepare_physics_assets() || !recovered.prepare_scene_resources()) {
            check(std::chrono::steady_clock::now() < until, "Map recovery resources timed out");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        detail::restore_runtime_tile_maps(recovered.engine.world(), tile_checkpoint);
        std::static_pointer_cast<NavigationRuntime>(recovered.engine.services().navigation())
            ->restore(nav_checkpoint);
        session.step();
        recovered.simulation.tick(1.f / 60.f);
        const auto expected = entity.get<LocalTranslation>();
        const auto actual = recovered.scene.entity(actor.entity.str()).get<LocalTranslation>();
        check(std::hypot(expected.x - actual.x, expected.y - actual.y) < .001 &&
                  expected.z == actual.z,
              "Fresh Map/navigation recovery changed the next route step");
    }
    for (unsigned i = 0; i < 900; ++i) {
        session.step();
        const auto at = entity.get<LocalTranslation>();
        check(at.z == initial.z, "Map navigation changed independent visual depth");
        if (!obstacle)
            check(at.y > -4 && at.y < -3, "Map floor agent left its selected solid strip");
    }
    check(navigation->agent_state(actor)->phase == NavigationAgentPhase::Arrived,
          "Map route failed to arrive: " +
              std::static_pointer_cast<NavigationRuntime>(navigation)->debug(entity.id()).dump());
}
void exercise(const std::filesystem::path& root, const std::filesystem::path& worker, bool obstacle,
              bool collect = false, bool dynamic = false) {
    auto catalog = AssetCatalog::open_project(root);
    AssetId map;
    for (const auto& [id, row] : catalog.records())
        if (row.type == TileMapAsset::type &&
            row.source == "Assets/Tiles/CollisionProfiles.tilemap.json")
            map = id;
    check(bool(map), "Missing original published collision Map fixture");
    EngineContext engine;
    Scene scene(engine.world());
    scene.restore_snapshot(
        {{"version", 1},
         {"entities",
          Json::array({{{"id", "floor"},
                        {"name", "Floor"},
                        {"components", {{"forge.position", {{"x", 0}, {"y", 0}, {"z", 0}}}}}},
                       {{"id", "map"},
                        {"name", "Selected collision layer"},
                        {"components", {{"forge.position", {{"x", 0}, {"y", 0}, {"z", 91}}}}}}})}});
    const auto floor = scene.entity("floor"), source = scene.entity("map");
    floor.set<LocalScale>({1, 1, 0});
    floor.set<NavigationRegion2D>({obstacle, false, 20, 20});
    TileMapRenderer renderer;
    renderer.map = {map};
    renderer.enabled = obstacle; // A navigation-only floor still needs candidate Map readiness.
    source.set<TileMapRenderer>(renderer);
    source.set<TileMapCollider2D>({1});
    source.set<LocalScale>({1, 1, 0});
    NavigationRegion2D region;
    region.source = 1;
    region.obstacle = obstacle;
    NavigationSettings settings;
    settings.plane = NavigationPlane::PlanarXY;
    settings.radius = .05f;
    settings.cell_size = .1f;
    settings.collect_static_colliders = collect;
    settings.dynamic_obstacles = dynamic;
    if (collect)
        source.set<PhysicsBody2D>({});
    else
        source.set<NavigationRegion2D>(region);
    const auto copied = capture(scene.effective_document(), settings);
    const auto candidates = engine.world()
                                .world()
                                .query_builder<const PersistentEntityId>()
                                .cache_kind(flecs::QueryCacheNone)
                                .query_flags(EcsQueryMatchPrefab | EcsQueryMatchDisabled)
                                .build();
    const auto native = capture_native(engine.world(), scene, candidates, settings);
    const auto inputs =
        tile_inputs(copied, [&](AssetId id) { return selected_tile_map_revision(catalog, {id}); });
    check(copied.maps == native.maps && copied.maps.size() == 1 &&
              source_digest(copied, inputs) == source_digest(native, inputs),
          "Native Map/selected-layer source capture differs from authored values");
    const auto record = prepare_navigation(root, scene.effective_document(), settings, worker)
                            .publish(scene.effective_document());
    check(record.schema_version == (collect ? 7u : 5u) &&
              record.metadata.at("tile_inputs") == inputs && record.dependency_edges.size() == 2,
          "Map source publication lost explicit selected resource provenance");
    const auto admitted = load(root, record);
    Query query(admitted.mesh);
    const auto route = obstacle ? query.path({0, -8, 7}, {0, 8, -19})
                                : query.path({-6.5, -3.5, 7}, {6.5, -3.5, -19});
    check(route.status == NavStatus::Success && route.points.size() >= (obstacle ? 3u : 2u),
          "Published Map collision geometry did not route across/around the selected floor");
    for (const auto& p : route.points)
        check(p[2] == 7, "Map-derived navigation changed visual depth");
    if (obstacle) {
        const auto ghost = query.project({.5, 2.5, 7});
        check(ghost.status == NavStatus::Success && ghost.points.size() == 1 &&
                  std::hypot(ghost.points[0][0] - .5, ghost.points[0][1] - 2.5) < .15,
              "A no-collision Tile definition incorrectly carved navigation");
    }
    const auto actor_id = authoring_command(scene, "entity.create", {{"name", "Map route agent"}})
                              .at("selected")
                              .get<std::string>();
    const auto actor = scene.entity(actor_id);
    actor.set<LocalTranslation>(obstacle ? LocalTranslation{0, -8, 7}
                                         : LocalTranslation{-6.5, -3.5, 7});
    actor.set<NavigationAgent>(
        {{record.id}, true, true, 3, .05f, obstacle ? 0.0 : 6.5, obstacle ? 8.0 : -3.5, -19});
    const auto path = std::filesystem::path("Scenes") /
                      (collect ? (dynamic ? "map-collected-dynamic.scene.json"
                                          : "map-collected-navigation.scene.json")
                               : (obstacle ? "map-mask-navigation.scene.json"
                                           : "map-floor-navigation.scene.json"));
    scene.save(root / path);
    const auto saved = scene.document();
    const auto source_ref = scene.reference(source.get<PersistentEntityId>().value.str());
    const auto actor_ref = scene.reference(actor_id);
    runtime(root, saved, record.id, source_ref, actor_ref, obstacle);
    Json unchanged;
    std::ifstream(root / path) >> unchanged;
    check(unchanged == saved, "Runtime Map edits or movement modified the authored Scene file");
    {
        ProjectLease lease(root);
        const std::array roots{scene.asset_id()};
        prepare_runtime_content_catalog(lease, roots, {}, scene.schema());
        const auto exported = root.parent_path() / AssetId::generate().str();
        package_runtime_content(root, exported, roots, {"windows", "d3d12"});
        const auto offline = root.parent_path() / AssetId::generate().str();
        std::filesystem::rename(root, offline);
        const auto catalog = open_runtime_content(exported, {"windows", "d3d12"});
        check(catalog.records().contains(record.id) && catalog.records().contains(map),
              "Map navigation export lost its selected resource closure");
        Json packaged;
        std::ifstream(exported / catalog.records().at(scene.asset_id()).source) >> packaged;
        runtime(exported, packaged, record.id, source_ref, actor_ref, obstacle);
        std::filesystem::rename(offline, root);
        std::filesystem::remove_all(exported);
    }
    auto changed = AssetCatalog::open_project(root);
    auto map_record = changed.records().at(map);
    map_record.metadata["forge.import"]["key"] = std::string(64, 'b');
    changed.replace(map_record);
    check(selected_geometry_digest(changed, copied, record.metadata) !=
              record.metadata.at("geometry_sha256"),
          "Selected Map revision did not stale navigation geometry");
    changed = AssetCatalog::open_project(root);
    const auto definition =
        std::find_if(changed.records().at(map).dependency_edges.begin(),
                     changed.records().at(map).dependency_edges.end(),
                     [](const auto& edge) { return edge.expected_type == TileSetAsset::type; });
    check(definition != changed.records().at(map).dependency_edges.end(),
          "Map fixture lacks definition dependency");
    auto definition_record = changed.records().at(definition->target);
    definition_record.metadata["forge.import"]["key"] = std::string(64, 'c');
    changed.replace(definition_record);
    rejects([&] { (void)selected_tile_map_revision(changed, {map}); });
    source.set<TileMapCollider2D>({0});
    rejects([&] { (void)prepare_navigation(root, scene.effective_document(), settings, worker); });
    check(AssetCatalog::open_project(root).records().at(record.id).metadata == record.metadata,
          "Rejected Map source changed the last published navigation selection");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 4, "Need Gallery, scratch root and worker");
        const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        exercise(root, std::filesystem::absolute(argv[3]), false);
        exercise(root, std::filesystem::absolute(argv[3]), true);
        exercise(root, std::filesystem::absolute(argv[3]), true, true);
        exercise(root, std::filesystem::absolute(argv[3]), true, true, true);
        std::filesystem::remove_all(root);
        std::cout << "Map source resources/native capture, floor/mask routes, provenance and "
                     "last-good PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
