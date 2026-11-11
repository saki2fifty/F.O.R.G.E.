#include "runtime_package.hpp"
#include "tile_asset_authoring.hpp"
#include <algorithm>
#include <forge/authoring.hpp>
#include <forge/game_session.hpp>
#include <forge/navigation_build.hpp>
#include <forge/physics_components.hpp>
#include <forge/primitive_catalog.hpp>
#include <forge/project_lease.hpp>
#include <forge/render_components.hpp>
#include <fstream>
#include <iostream>
#include <thread>
using namespace forge;
namespace {
constexpr const char* scene_path = "Scenes/137-navigation-collision-sources.scene.json";
void check(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
Json read(const std::filesystem::path& path) {
    Json result;
    std::ifstream(path) >> result;
    return result;
}
flecs::entity create(Scene& scene, const char* name, Double3 position,
                     unsigned primitive = no_primitive) {
    const auto id =
        authoring_command(
            scene, "entity.create",
            {{"name", name},
             {"position", {{"x", position[0]}, {"y", position[1]}, {"z", position[2]}}}})
            .at("selected")
            .get<std::string>();
    const auto entity = scene.entity(id);
    entity.set<Primitive>({primitive});
    return entity;
}
void visual(Scene& scene, const char* name, Double3 at, LocalScale scale, Tint tint,
            unsigned primitive = 0) {
    const auto entity = create(scene, name, at, primitive);
    entity.set<LocalScale>(scale);
    entity.set<Tint>(tint);
}
void prepare(const std::filesystem::path& root, const std::filesystem::path& worker) {
    auto lease = std::make_shared<ProjectLease>(root);
    auto asset_worker = worker.parent_path() / "forge_asset_build";
    asset_worker += worker.extension().string();
    AssetImportService imports(lease, tile_asset_import_registry(asset_worker),
                               {"windows", "d3d12", "desktop"});
    // The dependent Map and navigation must bind this build's selected Tile recipe.
    // A matching key is already current; retain its generation and immutable files.
    for (const bool map : {false, true}) {
        auto draft = imports.prepare(map ? "Assets/Tiles/CollisionProfiles.tilemap.json"
                                         : "Assets/Tiles/CollisionProfiles.tileset.json");
        const auto plan = draft.importer->discover(draft.request, {});
        const auto catalog = AssetCatalog::open_project(root);
        const auto& selected = catalog.records().at(draft.request.asset);
        if (selected.metadata.at("forge.import").at("key") == plan.input.key())
            continue;
        imports.submit(
            std::move(draft),
            [map](auto& candidate, const auto& input, const auto&) {
                if (map)
                    prepare_tile_map_publication(candidate, input);
                else
                    prepare_tile_set_publication(candidate, input);
            },
            [](const auto&, const auto&) {});
        check(imports.wait_idle(std::chrono::seconds(30)), "Gallery Tile publication timed out");
        const auto results = imports.poll();
        check(results.size() == 1 && results[0].published,
              "Gallery Tile publication failed" +
                  (results.empty() ? std::string{} : ": " + results[0].diagnostic));
    }
    EngineContext author;
    Scene scene(author.world());
    if (std::filesystem::exists(root / scene_path))
        scene.load(root / scene_path);
    else {
        NavigationRegion2D region;
        region.source = 1;
        create(scene, "Box floor - Collider 2D source", {-13, 0, 0})
            .set<BoxCollider2D>({20, 18})
            .set<NavigationRegion2D>(region);
        region.obstacle = true;
        create(scene, "Circle obstacle - radius 2m", {-16, 0, 0})
            .set<CircleCollider2D>({2})
            .set<NavigationRegion2D>(region);
        create(scene, "Capsule obstacle - radius 1.5m segment 3m", {-10, 0, 0})
            .set<CapsuleCollider2D>({1.5f, 3})
            .set<NavigationRegion2D>(region);
        create(scene, "Right rectangle floor", {12, 0, 0})
            .set<NavigationRegion2D>({true, false, 20, 18});
        AssetId map;
        const auto catalog = AssetCatalog::open_project(root);
        for (const auto& [id, record] : catalog.records())
            if (record.type == TileMapAsset::type &&
                record.source == "Assets/Tiles/CollisionProfiles.tilemap.json")
                map = id;
        check(bool(map), "Gallery collision Map is not published");
        TileMapRenderer renderer;
        renderer.map = {map};
        create(scene, "Map obstacle - selected collision layer", {12, 0, 0})
            .set<TileMapRenderer>(renderer)
            .set<TileMapCollider2D>({1})
            .set<NavigationRegion2D>(region);
        visual(scene, "Left floor visual", {-13, 0, 3}, {20, 18, .1f}, {.045f, .12f, .16f});
        visual(scene, "Right floor visual", {12, 0, 3}, {20, 18, .1f}, {.11f, .085f, .14f});
        visual(scene, "Circle visual", {-16, 0, 1}, {4, 4, .5f}, {.8f, .22f, .08f}, 1);
        visual(scene, "Capsule visual", {-10, 0, 1}, {6, 6, .5f}, {.8f, .22f, .08f}, 5);
        const std::array<Double3, 2> starts{{{-21, 0, -1}, {12, -8, -1}}};
        const std::array<Double3, 2> goals{{{-5, 0, -1}, {12, 8, -1}}};
        for (unsigned index = 0; index < 2; ++index) {
            const auto actor = create(
                scene, index ? "Amber - Map collision route" : "Cyan - circle and capsule route",
                starts[index]);
            actor.set<NavigationAgent>(
                {{}, true, true, 3, .06f, goals[index][0], goals[index][1], 19});
            const auto body =
                create(scene, index ? "Amber agent visual" : "Cyan agent visual", {}, 1);
            body.set<LocalScale>({.8f, .8f, .8f});
            body.set<Tint>(index ? Tint{1, .62f, .08f} : Tint{.06f, .82f, 1});
            scene.reparent_entity(body.get<PersistentEntityId>().value.str(),
                                  actor.get<PersistentEntityId>().value.str(),
                                  ReparentMode::KeepLocal);
            visual(scene, index ? "Amber destination" : "Cyan destination", goals[index],
                   {.4f, .4f, .4f}, index ? Tint{1, .8f, .35f} : Tint{.35f, .95f, 1}, 1);
        }
        Camera camera;
        camera.projection = unsigned(CameraProjection::Orthographic);
        camera.orthographic_height = 28;
        camera.aspect = 16.0 / 9.0;
        create(scene, "Overview Camera", {-.5, 0, -40}).set<Camera>(camera);
        create(scene, "Sunlight", {}).set<Light>({});
    }
    // Keep the ordinary visual primitive aligned with the configured physical contour.
    author.world().world().each([&](flecs::entity entity, const AuthoredName& name) {
        if (name.value == "Capsule visual")
            entity.set<LocalScale>({6, 6, .5f});
    });
    NavigationSettings settings;
    settings.plane = NavigationPlane::PlanarXY;
    settings.cell_size = .1f;
    settings.radius = .2f;
    std::optional<AssetRecord> current;
    const auto catalog = AssetCatalog::open_project(root);
    const auto document = scene.effective_document();
    for (const auto& [id, value] : catalog.records())
        if (value.type == NavMeshAsset::type &&
            value.metadata.at("source_scene").get<AssetId>() == scene.asset_id() &&
            value.metadata.at("settings") == Json(settings) &&
            Json(navigation_geometry_digest(root, document, value.metadata)) ==
                value.metadata.at("geometry_sha256") &&
            catalog.resolve(AssetRef<NavMeshAsset>{id}).state == AssetState::Available)
            current = value;
    const auto record =
        current ? *current : prepare_navigation(root, document, settings, worker).publish(document);
    author.world().world().each([&](flecs::entity entity, const NavigationAgent& value) {
        auto next = value;
        next.navmesh = {record.id};
        entity.set<NavigationAgent>(next);
    });
    scene.save(root / scene_path);
    const std::array roots{scene.asset_id()};
    prepare_runtime_content_catalog(*lease, roots, {}, scene.schema());
    std::cout << "Gallery137 collision-source courses and selected Map navigation published\n";
}
void exercise(const std::filesystem::path& root, const std::filesystem::path& path) {
    const auto saved = read(root / path);
    GameSessionConfig config;
    config.content_root = root;
    GameSession session(config);
    const auto candidate = session.prepare(saved);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!session.poll_preparation(candidate).ready) {
        check(std::chrono::steady_clock::now() < deadline,
              "Collision Gallery preparation timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    session.activate(candidate, {}, false);
    auto& world = session.active();
    std::array<flecs::entity, 2> actors;
    for (const auto& row : saved.at("entities")) {
        if (row.at("name") == "Cyan - circle and capsule route")
            actors[0] = world.scene.entity(row.at("id"));
        if (row.at("name") == "Amber - Map collision route")
            actors[1] = world.scene.entity(row.at("id"));
    }
    check(actors[0] && actors[1], "Collision Gallery agents missing");
    for (unsigned tick = 0; tick < 900; ++tick) {
        session.step();
        const auto left = actors[0].get<LocalTranslation>();
        const auto right = actors[1].get<LocalTranslation>();
        check(left.z == -1 && right.z == -1, "Collision Gallery changed independent visual depth");
        check(left.x >= -23 && left.x <= -3 && std::abs(left.y) <= 9 &&
                  std::hypot(left.x + 16, left.y) >= 2 &&
                  std::hypot(left.x + 10, left.y - std::clamp(left.y, -1.5, 1.5)) >= 1.5,
              "Cyan route left its Box floor or crossed analytic Circle/Capsule geometry");
        check(!(right.x > 5 && right.x < 19 && right.y > -4 && right.y < -3),
              "Amber route crossed the selected Map's full-cell collision strip");
    }
    const auto nav = world.engine.services().navigation();
    for (const auto actor : actors)
        check(nav->agent_state(world.scene.reference(actor.get<PersistentEntityId>().value.str()))
                      ->phase == NavigationAgentPhase::Arrived,
              "Collision Gallery agent did not reach its destination");
    check(read(root / path) == saved, "Collision Gallery gameplay modified saved source");
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 4 && std::string_view(argv[1]) == "--prepare") {
            prepare(std::filesystem::absolute(argv[2]), std::filesystem::absolute(argv[3]));
            return 0;
        }
        check(argc == 3, "Need Gallery and collision-source scratch root");
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto root = scratch / "Gallery";
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        exercise(root, scene_path);
        const auto scene = read(root / scene_path);
        const std::array roots{scene.at("asset_id").get<AssetId>()};
        const auto output = scratch / "Export";
        package_runtime_content(root, output, roots, {"windows", "d3d12"});
        const auto moved = scratch / "Relocated", offline = scratch / "SourcesOffline";
        std::filesystem::rename(output, moved);
        std::filesystem::rename(root, offline);
        const auto catalog = open_runtime_content(moved, {"windows", "d3d12"});
        exercise(moved, catalog.records().at(roots[0]).source);
        std::filesystem::remove_all(scratch);
        std::cout << "Gallery137: analytic collision exclusion, actual GameSession arrival, source "
                     "preservation and source-free relocated export PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
