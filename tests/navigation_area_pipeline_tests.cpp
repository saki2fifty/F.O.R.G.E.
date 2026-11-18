#include "editor/document.hpp"
#include "navigation_area_value.hpp"
#include "navigation_asset.hpp"
#include "navigation_behavior_authoring.hpp"
#include "navigation_sources.hpp"
#include <forge/authoring.hpp>
#include <forge/character_components.hpp>
#include <forge/navigation.hpp>
#include <forge/physics.hpp>
#include <forge/runtime.hpp>
#include <forge/runtime_resources.hpp>
#include <fstream>
#include <iostream>
#include <thread>
using namespace forge;
using namespace forge::navigation_detail;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void rejects(F call) {
    try {
        call();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid area pipeline operation was accepted");
}
std::string create(Scene& scene, const char* name) {
    return authoring_command(scene, "entity.create", {{"name", name}, {"kind", 4}})
        .at("selected")
        .get<std::string>();
}
struct Runtime {
    EngineContext engine;
    Scene scene;
    RuntimeSimulation simulation;
    std::shared_ptr<NavigationRuntime> navigation;
    Runtime(const std::filesystem::path& root, const Json& snapshot, bool physical = false,
            bool resources = false)
        : engine(WorldRole::Runtime, false,
                 [&] {
                     std::vector<EngineModule> modules;
                     if (resources)
                         modules.push_back(runtime_resources_module(root));
                     modules.push_back(navigation_module(root));
                     if (physical)
                         modules.push_back(physics_module({}, root));
                     return modules;
                 }()),
          scene(engine.world()), simulation(engine.world(), scene),
          navigation(std::static_pointer_cast<NavigationRuntime>(engine.services().navigation())) {
        scene.restore_snapshot(snapshot);
    }
    void step() { simulation.tick(1.f / 60); }
};
#include "navigation_area_retained_tests.hpp"
void exercise(const std::filesystem::path& root, const std::filesystem::path& worker,
              NavigationPlane plane, bool dynamic, bool prepare_only = false) {
    SceneDocument::create_project(root, "Navigation routing policies");
    EngineContext authoring;
    Scene scene(authoring.world());
    SceneDocument project(scene);
    project.open_project(root);
    const auto planar = plane == NavigationPlane::PlanarXY;
    const auto point = [&](double x, double horizontal) {
        return planar ? Double3{x, horizontal, 7} : Double3{x, .1, horizontal};
    };
    std::string classified;
    for (bool overlay : {false, true}) {
        const auto id = create(scene, overlay ? "Area 2" : "Floor");
        auto entity = scene.entity(id);
        entity.set<LocalTranslation>({0, 0, 0});
        if (planar)
            entity.set<NavigationRegion2D>(
                {true, false, overlay ? 3.f : 18.f, overlay ? 6.f : 12.f});
        else
            entity.set<Primitive>({3})
                .set<LocalScale>({overlay ? 3.f : 18.f, 1, overlay ? 6.f : 12.f})
                .set<NavigationSurface>({});
        if (overlay) {
            entity.set<NavigationArea>({2});
            classified = id;
        }
    }
    project.save();
    NavigationSettings settings;
    settings.plane = plane;
    settings.dynamic_obstacles = dynamic;
    const auto selected = capture(scene.effective_document(), settings);
    auto candidates = authoring.world().world().query<const PersistentEntityId>();
    const auto native = capture_native(authoring.world(), scene, candidates, settings);
    check(area_sources(selected) == area_sources(native) &&
              source_digest(selected, Json::array()) == source_digest(native, Json::array()),
          "Effective and native source classification disagree");
    const auto record = prepare_navigation(root, scene.effective_document(), settings, worker)
                            .publish(scene.effective_document());
    check(record.schema_version == 9 &&
              record.metadata.at("area_sources") == area_sources(selected) &&
              AssetCatalog::open_project(root).resolve(AssetRef<NavMeshAsset>{record.id}).state ==
                  AssetState::Available,
          "Real worker/candidate/catalog lost the classified profile");
    // Build-only source data must participate in the existing stale-candidate boundary.
    std::vector<std::uint32_t> visible_areas;
    const auto visible_triangles = navigation_triangles(root, {record.id}, visible_areas);
    check(visible_triangles.size() == visible_areas.size() * 3 &&
              std::ranges::find(visible_areas, 2u) != visible_areas.end(),
          "Read-only Scene visualization lost its admitted polygon classes");
    auto stale = prepare_navigation(root, scene.effective_document(), settings, worker);
    scene.entity(classified).set<NavigationArea>({3});
    rejects([&] { stale.publish(scene.effective_document()); });
    scene.entity(classified).set<NavigationArea>({2});
    check(AssetCatalog::open_project(root).records().at(record.id).source == record.source,
          "A stale area candidate replaced the accepted immutable artifact");
    const auto direct_id = create(scene, "Direct agent"),
               filtered_id = create(scene, "Filtered agent");
    const auto start = point(-7, 0), end = point(7, 0);
    for (const auto& id : {direct_id, filtered_id})
        scene.entity(id)
            .set<LocalTranslation>({start[0], start[1], start[2]})
            .set<NavigationAgent>({{record.id}, true, true, 4, .1f, end[0], end[1], end[2]});
    NavigationAreaFilter expensive;
    expensive.costs[2] = 20;
    scene.entity(filtered_id).set<NavigationAreaFilter>(expensive);
    project.save();
    const auto authored = scene.snapshot();
    if (prepare_only) {
        std::ofstream(root / "scene.json") << authored.dump(2) << '\n';
        std::cout << root.string() << '\n';
        return;
    }
    Runtime run(root, authored);
    const auto filtered = run.scene.entity(filtered_id), direct = run.scene.entity(direct_id);
    const auto ref = run.engine.world().reference(filtered.id()).value();
    const auto direct_ref = run.engine.world().reference(direct.id()).value();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    do {
        run.step();
        if (run.navigation->agent_state(ref)->has_path)
            break;
        check(std::chrono::steady_clock::now() < deadline, "Area runtime readiness timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (true);
    const auto routed = run.navigation->debug(filtered.id());
    const auto straight = run.navigation->debug(direct.id());
    check(routed.at("path").size() > 2 && straight.at("path").size() == 2,
          "Agents sharing one NavMesh did not use their independent costs");
    check(run.navigation->find_path({record.id}, start, end, expensive).points.size() > 2 &&
              run.navigation->find_path({record.id}, start, end).points.size() == 2,
          "Public filtered query did not share the runtime routing owner");
    const auto tune = [&](const char* field, const Json& value) {
        return run.navigation->tune(ref, "forge.navigation_area_filter", field, value);
    };
    const auto before = filtered.get<NavigationAreaFilter>();
    for (const auto& invalid : {Json(-1), Json(65536), Json(true)}) {
        rejects([&] { tune("mask", invalid); });
        check(filtered.get<NavigationAreaFilter>() == before,
              "Invalid live mask partially applied");
    }
    auto bad_costs = area_filter_value(expensive).at("costs");
    bad_costs[2] = 0;
    rejects([&] { tune("costs", bad_costs); });
    check(filtered.get<NavigationAreaFilter>() == before, "Invalid live cost partially applied");
    tune("mask", 0);
    check(run.navigation->agent_state(ref)->phase == NavigationAgentPhase::Pending &&
              run.navigation->agent_state(ref)->tick == 0,
          "Live policy edit retained an observation for the superseded route");
    run.engine.world().evaluate_world_transforms();
    const auto stopped = filtered.get<WorldTransform>().affine.point({0, 0, 0});
    run.step();
    run.engine.world().evaluate_world_transforms();
    check(run.navigation->agent_state(ref)->phase == NavigationAgentPhase::Failed &&
              filtered.get<WorldTransform>().affine.point({0, 0, 0}) == stopped &&
              run.navigation->agent_state(direct_ref)->phase == NavigationAgentPhase::Moving,
          "Empty policy mask moved its Agent or stopped another Agent");
    tune("mask", 65535);
    tune("costs", area_filter_value(NavigationAreaFilter{}).at("costs"));
    run.step();
    check(run.navigation->debug(filtered.id()).at("path").size() == 2 &&
              run.navigation->agent_state(ref)->phase == NavigationAgentPhase::Moving,
          "Live policy changes did not replan at the next fixed tick");
    const auto checkpoint = run.navigation->checkpoint();
    check(checkpoint.at("version").get<unsigned>() == 13 && checkpoint.at("agents").size() == 2,
          "Mixed default/filtered Agent recovery lost its explicit profile");
    Runtime resumed(root, authored);
    resumed.navigation->restore(checkpoint);
    check(resumed.scene.entity(filtered_id).get<NavigationAreaFilter>() == NavigationAreaFilter{},
          "Fresh recovery did not restore runtime-owned cost edits");
    resumed.step();
    check(resumed.navigation->checkpoint().at("version").get<unsigned>() == 13,
          "Fresh recovered policy lost its presence or ownership");
    auto reject_recovery = [&](Json damaged) {
        Runtime candidate(root, authored);
        rejects([&] { candidate.navigation->restore(damaged); });
    };
    auto damaged = checkpoint;
    damaged["version"] = 12;
    reject_recovery(damaged);
    auto selected_entry = std::ranges::find_if(
        checkpoint.at("agents"), [](const auto& entry) { return !entry.at("filter").is_null(); });
    const auto index = std::size_t(selected_entry - checkpoint.at("agents").begin());
    for (const auto& invalid : {Json(-1), Json(65536), Json(.5), Json(true)}) {
        damaged = checkpoint;
        damaged["agents"][index]["filter"]["config"]["mask"] = invalid;
        reject_recovery(damaged);
    }
    damaged = checkpoint;
    damaged["agents"][index]["filter"]["config"]["costs"].erase(
        damaged["agents"][index]["filter"]["config"]["costs"].end() - 1);
    reject_recovery(damaged);
    damaged = checkpoint;
    damaged["agents"][index]["filter"]["owned"] = false;
    reject_recovery(damaged);
    damaged = checkpoint;
    damaged["agents"][index].erase("filter");
    reject_recovery(damaged);
    check(scene.snapshot() == authored, "Runtime area tuning changed authored Scene data");
    std::cout << (planar ? "XY" : "3D") << (dynamic ? " dynamic" : " static")
              << " worker/catalog/native capture/Agents/live/recovery passed\n";
}
#include "navigation_area_behavior_tests.hpp"
#include "navigation_area_physical_tests.hpp"
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3 || (argc == 4 && (std::string_view(argv[3]) == "--prepare-sdk-spatial" ||
                                          std::string_view(argv[3]) == "--prepare-sdk-planar" ||
                                          std::string_view(argv[3]) == "--avoidance-consumers")),
              "Need project scratch parent, navigation worker and optional SDK plane");
        const auto base = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(base);
        if (argc == 4 && std::string_view(argv[3]) == "--avoidance-consumers") {
            for (auto plane : {NavigationPlane::Spatial3D, NavigationPlane::PlanarXY}) {
                physical_policies(base / (std::to_string(unsigned(plane)) + "-physical"),
                                  std::filesystem::absolute(argv[2]), plane, true);
                behavior_policies(base / (std::to_string(unsigned(plane)) + "-behavior"),
                                  std::filesystem::absolute(argv[2]), plane, true);
            }
            return 0;
        }
        if (argc == 4) {
            exercise(base / "SDK", std::filesystem::absolute(argv[2]),
                     std::string_view(argv[3]) == "--prepare-sdk-planar"
                         ? NavigationPlane::PlanarXY
                         : NavigationPlane::Spatial3D,
                     true, true);
            return 0;
        }
        for (auto plane : {NavigationPlane::Spatial3D, NavigationPlane::PlanarXY}) {
            for (bool dynamic : {false, true}) {
                exercise(base /
                             (std::to_string(unsigned(plane)) + (dynamic ? "-tiled" : "-static")),
                         std::filesystem::absolute(argv[2]), plane, dynamic);
                retained_policy(base / (std::to_string(unsigned(plane)) +
                                        (dynamic ? "-retained-tiled" : "-retained-static")),
                                std::filesystem::absolute(argv[2]), plane, dynamic);
            }
            physical_policies(base / (std::to_string(unsigned(plane)) + "-physical"),
                              std::filesystem::absolute(argv[2]), plane);
            behavior_policies(base / (std::to_string(unsigned(plane)) + "-behavior"),
                              std::filesystem::absolute(argv[2]), plane);
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
