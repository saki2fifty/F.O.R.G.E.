#include "navigation_asset.hpp"
#include <algorithm>
#include <cmath>
#include <forge/authoring.hpp>
#include <forge/navigation.hpp>
#include <forge/runtime.hpp>
#include <fstream>
#include <iostream>

using namespace forge;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void rejects(F&& fn) {
    try {
        fn();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Expected navigation profile rejection");
}
std::string read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
flecs::entity create(Scene& scene, const char* name, Double3 position = {}) {
    const auto result = authoring_command(scene, "entity.create", {{"name", name}, {"kind", 4}});
    const auto entity = scene.entity(result.at("selected").get<std::string>());
    entity.set<LocalTranslation>({position[0], position[1], position[2]});
    return entity;
}
Json row(const AssetCatalog& catalog, AssetId id) {
    const auto document = catalog.document();
    for (const auto& value : document.at("assets"))
        if (value.at("id").get<AssetId>() == id)
            return value;
    throw std::runtime_error("Profile asset record missing");
}
void run(const std::filesystem::path& root, const std::filesystem::path& worker) {
    const Json legacy = {{"radius", .4f}, {"height", 2.f},    {"climb", .4f},
                         {"slope", 45.f}, {"cell_size", .2f}, {"cell_height", .1f}};
    check(Json(NavigationSettings{}) == legacy &&
              legacy.get<NavigationSettings>().profile == "default",
          "Default profile changed legacy settings bytes or admission");
    for (const Json& invalid :
         {Json(""), Json("../large"), Json("two words"), Json("1large"), Json("caf\xc3\xa9"),
          Json(std::string(65, 'x')), Json(7), Json(nullptr)}) {
        auto candidate = legacy;
        candidate["profile"] = invalid;
        rejects([&] { (void)candidate.get<NavigationSettings>(); });
    }
    EngineContext author;
    Scene scene(author.world());
    create(scene, "Walkable XY floor").set<NavigationRegion2D>({true, false, 20, 12});
    for (float sign : {-1.f, 1.f})
        create(scene, sign < 0 ? "Lower wall" : "Upper wall", {0, sign * 3.75, 0})
            .set<NavigationRegion2D>({true, true, 2, 4.5f});
    create(scene, "Independent spatial floor", {0, -1, 0})
        .set<Primitive>({3})
        .set<LocalScale>({20, 1, 20})
        .set<NavigationSurface>({true});
    NavigationSettings small;
    small.plane = NavigationPlane::PlanarXY;
    small.radius = .2f;
    small.cell_size = .1f;
    const auto publish = [&](const NavigationSettings& settings) {
        const auto effective = scene.effective_document();
        return prepare_navigation(root, effective, settings, worker).publish(effective);
    };
    const auto first = publish(small);
    const auto original_catalog = AssetCatalog::open_project(root);
    const auto original_default = row(original_catalog, first.id);
    const auto original_bytes = read(root / first.source);
    NavigationSettings large = small;
    large.profile = "Large_agent-2";
    large.radius = 1.8f;
    check(Json(large).get<NavigationSettings>() == large,
          "Named profile did not round-trip with its settings");
    const auto second = publish(large);
    auto catalog = AssetCatalog::open_project(root);
    check(first.id != second.id && row(catalog, first.id) == original_default &&
              read(root / first.source) == original_bytes &&
              navigation_profiles(catalog, scene.asset_id()) ==
                  std::map<std::string, AssetId>{{"default", first.id}, {large.profile, second.id}},
          "Publishing another profile replaced default identity, selection or artifact");
    navigation_detail::Query small_query(navigation_detail::load(root, first).mesh);
    navigation_detail::Query large_query(navigation_detail::load(root, second).mesh);
    check(small_query.path({-7, 0, 9}, {7, 0, 9}).status == NavStatus::Success,
          "Small profile cannot cross the narrow passage");
    const auto blocked = large_query.path({-7, 0, 9}, {7, 0, 9});
    check(blocked.status == NavStatus::Partial || blocked.status == NavStatus::NoPath,
          "Large profile ignored its clearance or was not independently queryable");
    check(large_query.path({-7, 0, 9}, {-4, 0, 9}).status == NavStatus::Success,
          "Large profile lost its reachable side of the floor");
    const auto small_actor = create(scene, "Small profile agent", {-7, 0, 9});
    const auto large_actor = create(scene, "Large profile agent", {-7, 3, 19});
    small_actor.set<NavigationAgent>({{first.id}, true, true, 4, .1f, 7, 0, -9});
    large_actor.set<NavigationAgent>({{second.id}, true, true, 4, .1f, -4, 3, -19});
    {
        EngineContext context(WorldRole::Runtime, false, {navigation_module(root)});
        Scene live(context.world());
        live.restore_snapshot(scene.snapshot());
        RuntimeSimulation simulation(context.world(), live);
        const auto small_id = small_actor.get<PersistentEntityId>().value;
        const auto large_id = large_actor.get<PersistentEntityId>().value;
        for (unsigned tick = 0; tick < 360; ++tick)
            simulation.tick(1.f / 60);
        const auto navigation = context.world().services().navigation();
        check(navigation->agent_state(live.reference(small_id.str()))->phase ==
                      NavigationAgentPhase::Arrived &&
                  navigation->agent_state(live.reference(large_id.str()))->phase ==
                      NavigationAgentPhase::Arrived,
              "Agents using different profiles did not independently arrive");
        check(navigation->find_path({second.id}, {-7, 0, 9}, {7, 0, 9}).status !=
                  NavStatus::Success,
              "Runtime large-profile query used the small-profile cache");
    }
    {
        EngineContext context(WorldRole::Runtime, false, {navigation_module(root)});
        Scene live(context.world());
        live.restore_snapshot(scene.snapshot());
        RuntimeSimulation simulation(context.world(), live);
        const auto a = live.entity(small_actor.get<PersistentEntityId>().value.str());
        const auto b = live.entity(large_actor.get<PersistentEntityId>().value.str());
        a.set<LocalTranslation>({-8, 0, 9})
            .set<NavigationAgent>({{first.id}, true, true, 2, .1f, -2, 0, 9})
            .set<NavigationAvoidance>({true, 2, 1});
        b.set<LocalTranslation>({-5, 0, 19})
            .set<NavigationAgent>({{second.id}, true, false})
            .set<NavigationAvoidance>({true, 2, 1});
        double minimum = 100, deviation = 0;
        for (unsigned tick = 0; tick < 600; ++tick) {
            simulation.tick(1.f / 60);
            const auto position = a.get<LocalTranslation>();
            minimum = std::min(minimum, std::hypot(position.x + 5, position.y));
            deviation = std::max(deviation, std::abs(position.y));
        }
        const auto navigation = context.world().services().navigation();
        check(navigation->agent_state(live.reference(a.get<PersistentEntityId>().value.str()))
                          ->phase == NavigationAgentPhase::Arrived &&
                  minimum > 1.8 && deviation > 1.5 && b.get<LocalTranslation>().x == -5 &&
                  b.get<LocalTranslation>().y == 0,
              "Small agent ignored the large-radius peer on another profile");
        std::cout << "Cross-profile avoidance minimum=" << minimum << " deviation=" << deviation
                  << '\n';
    }
    const auto second_row = row(catalog, second.id);
    small.radius = .25f;
    const auto rebuilt = publish(small);
    catalog = AssetCatalog::open_project(root);
    check(rebuilt.id == first.id && rebuilt.source != first.source &&
              row(catalog, second.id) == second_row && read(root / first.source) == original_bytes,
          "Rebuilding default changed a named profile or removed its prior artifact");
    auto spatial = small;
    spatial.plane = NavigationPlane::Spatial3D;
    const auto switched = publish(spatial);
    catalog = AssetCatalog::open_project(root);
    check(switched.id == first.id && row(catalog, second.id) == second_row,
          "Legacy plane switch changed default ID or another profile");
    {
        EngineContext context(WorldRole::Runtime, false, {navigation_module(root)});
        Scene live(context.world());
        live.restore_snapshot(scene.snapshot());
        RuntimeSimulation simulation(context.world(), live);
        const auto a = live.entity(small_actor.get<PersistentEntityId>().value.str());
        const auto b = live.entity(large_actor.get<PersistentEntityId>().value.str());
        navigation_detail::Query spatial_query(navigation_detail::load(root, switched).mesh);
        const auto start = spatial_query.project({-8, -1, 0});
        const auto goal = spatial_query.project({2, -1, 0});
        check(start.status == NavStatus::Success && goal.status == NavStatus::Success,
              "Spatial profile anchors cannot be projected");
        a.set<LocalTranslation>({start.points[0][0], start.points[0][1], start.points[0][2]})
            .set<NavigationAgent>({{switched.id},
                                   true,
                                   true,
                                   3,
                                   .1f,
                                   goal.points[0][0],
                                   goal.points[0][1],
                                   goal.points[0][2]})
            .set<NavigationAvoidance>({true, 2, 1});
        b.set<LocalTranslation>({-5, 0, 0})
            .set<NavigationAgent>({{second.id}, true, false})
            .set<NavigationAvoidance>({true, 2, 1});
        for (unsigned tick = 0; tick < 300; ++tick) {
            simulation.tick(1.f / 60);
            check(std::abs(a.get<LocalTranslation>().z) < .0001,
                  "Spatial avoidance used a peer from the incompatible XY plane");
        }
        check(context.world()
                      .services()
                      .navigation()
                      ->agent_state(live.reference(a.get<PersistentEntityId>().value.str()))
                      ->phase == NavigationAgentPhase::Arrived,
              "Cross-plane isolation stopped a valid spatial route");
    }
    // Another Scene may reuse the same name without sharing identity.
    EngineContext other_author;
    Scene other(other_author.world());
    create(other, "Other floor").set<NavigationRegion2D>({true, false, 20, 12});
    const auto other_doc = other.effective_document();
    const auto other_record = prepare_navigation(root, other_doc, large, worker).publish(other_doc);
    check(other_record.id != second.id &&
              navigation_profiles(AssetCatalog::open_project(root), other.asset_id()).size() == 1,
          "A profile name escaped its Scene scope");
    catalog = AssetCatalog::open_project(root);
    auto damaged = catalog.records().at(second.id);
    damaged.metadata["settings"]["radius"] = 999;
    catalog.replace(damaged);
    catalog.save(AssetCatalog::project_index(root));
    check(navigation_profiles(catalog, scene.asset_id()).at(large.profile) == second.id,
          "Invalid numeric settings obscured the existing profile identity");
    const auto repaired = publish(large);
    check(repaired.id == second.id && repaired.metadata.at("settings") == Json(large) &&
              std::filesystem::is_regular_file(root / second.source),
          "Repairing numeric settings changed profile identity or removed the prior artifact");
    catalog = AssetCatalog::open_project(root);
    auto duplicate = second;
    duplicate.id = AssetId::generate();
    duplicate.source = std::filesystem::path("Assets/Navigation/Generated") / duplicate.id.str() /
                       "duplicate.fnav";
    duplicate.metadata["asset_id"] = duplicate.id;
    catalog.add(duplicate);
    catalog.save(AssetCatalog::project_index(root));
    const auto ambiguous = read(AssetCatalog::project_index(root));
    rejects([&] { (void)navigation_profiles(catalog, scene.asset_id()); });
    rejects([&] { (void)publish(large); });
    check(read(AssetCatalog::project_index(root)) == ambiguous &&
              read(root / first.source) == original_bytes,
          "Duplicate profile rejection changed catalog or old immutable data");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need scratch root and matching navigation worker");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        run(root, std::filesystem::absolute(argv[2]));
        std::cout
            << "Navigation profile identity, legacy representation, clearance and scope PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
