#include "navigation_asset.hpp"
#include "navigation_sources.hpp"
#include <forge/authoring.hpp>
#include <forge/navigation_build.hpp>
#include <forge/physics_components.hpp>
#include <forge/runtime.hpp>
#include <iostream>
using namespace forge;
using namespace forge::navigation_detail;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
void exercise(const std::filesystem::path& root, const std::filesystem::path& worker,
              unsigned kind) {
    EngineContext engine;
    Scene scene(engine.world());
    scene.restore_snapshot(
        {{"version", 1},
         {"entities",
          Json::array({{{"id", "floor"},
                        {"name", "Floor"},
                        {"components", {{"forge.position", {{"x", 0}, {"y", 0}, {"z", 0}}}}}},
                       {{"id", "shape"},
                        {"name", "Physical source"},
                        {"components", {{"forge.position", {{"x", 0}, {"y", 0}, {"z", 91}}}}}}})}});
    const auto floor = scene.entity("floor"), shape = scene.entity("shape");
    floor.set<LocalScale>({1, 1, 0});
    floor.set<NavigationRegion2D>({true, false, 20, 20});
    NavigationRegion2D region;
    region.source = 1;
    region.obstacle = true;
    shape.set<NavigationRegion2D>(region);
    if (kind == 0)
        shape.set<BoxCollider2D>({2, 4});
    else if (kind == 1)
        shape.set<CircleCollider2D>({2});
    else
        shape.set<CapsuleCollider2D>({1, 4});
    auto legacy = region;
    legacy.source = 0;
    shape.set<NavigationRegion2D>(legacy);
    NavigationSettings settings;
    settings.plane = NavigationPlane::PlanarXY;
    const auto previous = prepare_navigation(root, scene.effective_document(), settings, worker)
                              .publish(scene.effective_document());
    const auto previous_size = std::filesystem::file_size(root / previous.source);
    check(previous.schema_version == 3, "Legacy source no longer selected geometry3");
    shape.set<NavigationRegion2D>(region);
    engine.world().evaluate_world_transforms();
    const auto before = scene.document();
    {
        auto old = before;
        for (auto& row : old["entities"])
            if (row["components"].contains("forge.navigation_region_2d")) {
                row["components"]["forge.navigation_region_2d"].erase("source");
                row["components"]["forge.navigation_region_2d"].erase("points");
            }
        EngineContext compatible;
        Scene restored(compatible.world());
        restored.reset(old);
        const auto effective = restored.effective_document();
        for (const auto& row : effective.at("entities")) {
            const auto value = restored.entity(row.at("id")).get<NavigationRegion2D>();
            check(value.source == 0 && value.points.empty(),
                  "Old saved regions lost their explicit metadata defaults");
        }
    }
    const auto copied = capture(scene.effective_document(), NavigationPlane::PlanarXY);
    const auto candidates = engine.world()
                                .world()
                                .query_builder<const PersistentEntityId>()
                                .cache_kind(flecs::QueryCacheNone)
                                .query_flags(EcsQueryMatchPrefab | EcsQueryMatchDisabled)
                                .build();
    const auto native =
        capture_native(engine.world(), scene, candidates, NavigationPlane::PlanarXY);
    const auto a = geometry(copied), b = geometry(native);
    check(a.digest == b.digest && a.vertices == b.vertices && a.obstacles == b.obstacles &&
              scene.document() == before,
          "Native collider source capture disagrees with authored input");
    check(a.obstacles.size() == 1 && a.obstacles[0].size() >= 12,
          "Convex physical contour was expanded into separate obstacle triangles");
    auto candidate = prepare_navigation(root, scene.effective_document(), settings, worker);
    auto unused = region;
    unused.width = 31;
    unused.height = 29;
    shape.set<NavigationRegion2D>(unused);
    const auto record = candidate.publish(scene.effective_document());
    check(record.id == previous.id &&
              std::filesystem::file_size(root / previous.source) == previous_size,
          "Collider migration changed the Scene's logical navigation ID or discarded its prior "
          "artifact");
    check(record.schema_version == 5 && record.metadata.at("geometry_version") == 5 &&
              record.metadata.at("tile_inputs").empty(),
          "Collider provenance did not select explicit geometry5");
    const auto admitted = load(root, record);
    const auto route = Query(admitted.mesh).path({-8, 0, 7}, {8, 0, -19});
    check(route.status == NavStatus::Success && route.points.size() > 2,
          "Published collider source did not produce a real route");
    const auto id = shape.get<PersistentEntityId>().value.str();
    authoring_command(scene, "property.set",
                      {{"entity", id},
                       {"component", "forge.navigation_region_2d"},
                       {"field", "source"},
                       {"value", 0u}});
    check(shape.get<NavigationRegion2D>().source == 0 && scene.undo() &&
              scene.entity(id).get<NavigationRegion2D>().source == 1 && scene.redo() &&
              scene.entity(id).get<NavigationRegion2D>().source == 0,
          "Shape-source authoring lost native value or Undo/Redo");
    const auto stable = scene.document();
    const auto revision = scene.revision();
    bool rejected = false;
    try {
        authoring_command(scene, "property.set",
                          {{"entity", id},
                           {"component", "forge.navigation_region_2d"},
                           {"field", "source"},
                           {"value", 2u}});
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected && scene.document() == stable && scene.revision() == revision,
          "Unsupported collider source changed authored state");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need collider source scratch root and native worker");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        for (unsigned kind : {0u, 1u, 2u}) {
            const auto path = root / std::to_string(kind);
            std::filesystem::create_directories(path);
            exercise(path, std::filesystem::absolute(argv[2]), kind);
        }
        std::filesystem::remove_all(root);
        std::cout << "Reflected collider sources, native/copy capture, geometry5 publication and "
                     "history PASS\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
