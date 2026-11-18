#include "navigation_asset.hpp"
#include "runtime_package.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <forge/authoring.hpp>
#include <forge/game_session.hpp>
#include <forge/navigation.hpp>
#include <forge/navigation_build.hpp>
#include <fstream>
#include <iostream>
#include <optional>
#include <string_view>
#include <thread>
using namespace forge;
namespace {
constexpr std::array area_paths{"Scenes/231-navigation-area-costs.scene.json",
                                "Scenes/232-navigation-area-costs-xy.scene.json"};
constexpr std::array policy_paths{"Scenes/233-navigation-avoidance-policies.scene.json",
                                  "Scenes/234-navigation-avoidance-policies-xy.scene.json"};
constexpr std::array names{"Cyan - unit costs", "Orange - Area 2 cost 20",
                           "Violet - default area only"};
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
Json read(const std::filesystem::path& path) {
    Json value;
    std::ifstream(path) >> value;
    return value;
}
void prepare(const std::filesystem::path& root, const std::filesystem::path& worker, bool peers) {
    const auto& paths = peers ? policy_paths : area_paths;
    const auto lease = std::make_shared<ProjectLease>(root);
    for (unsigned plane = 0; plane < paths.size(); ++plane) {
        EngineContext engine;
        Scene scene(engine.world());
        scene.load(root / paths[plane]);
        NavigationSettings settings;
        settings.plane = plane ? NavigationPlane::PlanarXY : NavigationPlane::Spatial3D;
        settings.dynamic_obstacles = !peers;
        const auto effective = scene.effective_document();
        const auto catalog = AssetCatalog::open_project(root);
        const auto profiles = navigation_profiles(catalog, scene.asset_id());
        std::optional<AssetRecord> current;
        if (const auto found = profiles.find("default"); found != profiles.end()) {
            const auto& selected = catalog.records().at(found->second);
            if ((peers || selected.schema_version == 9) &&
                selected.metadata.at("settings") == Json(settings) &&
                navigation_geometry_digest(root, effective, selected.metadata) ==
                    selected.metadata.at("geometry_sha256").get<std::string>()) {
                (void)navigation_detail::load(root, selected);
                current = selected;
            }
        }
        const auto record =
            current ? *current
                    : prepare_navigation(root, effective, settings, worker).publish(effective);
        check(peers || record.schema_version == 9,
              "Area Gallery did not publish the classified profile");
        engine.world().world().each([&](flecs::entity entity, const NavigationAgent& value) {
            auto agent = value;
            agent.navmesh = {record.id};
            entity.set<NavigationAgent>(agent);
        });
        scene.save(root / paths[plane]);
        const std::array roots{scene.asset_id()};
        prepare_runtime_content_catalog(*lease, roots, {}, scene.schema());
    }
}
void exercise(const std::filesystem::path& root, const std::filesystem::path& path, bool planar) {
    const auto saved = read(root / path);
    GameSessionConfig configuration;
    configuration.content_root = root;
    GameSession session(configuration);
    const auto candidate = session.prepare(saved);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!session.poll_preparation(candidate).ready) {
        check(std::chrono::steady_clock::now() < deadline, "Area Gallery resources timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    session.activate(candidate, {}, false);
    auto& world = session.active();
    std::array<flecs::entity, 3> actors;
    for (const auto& row : saved.at("entities"))
        for (unsigned i = 0; i < names.size(); ++i)
            if (row.at("name").get<std::string>() == names[i])
                actors[i] = world.scene.entity(row.at("id").get<std::string>());
    for (const auto actor : actors)
        check(actor && actor.has<NavigationAreaFilter>() && actor.has<NavigationBehavior>(),
              "Area Gallery lost editable filters or shared patrol behavior");
    const auto mesh = actors[0].get<NavigationAgent>().navmesh;
    check(mesh.id && actors[1].get<NavigationAgent>().navmesh == mesh &&
              actors[2].get<NavigationAgent>().navmesh == mesh,
          "Area Gallery agents duplicated their shared NavMesh");
    const auto navigation = world.engine.services().navigation();
    const auto runtime = std::static_pointer_cast<NavigationRuntime>(navigation);
    std::array<bool, 3> right{}, returned{};
    double cost_extent = 0, excluded_extent = 0;
    bool direct_crossed = false;
    for (unsigned tick = 0; tick < 1000; ++tick) {
        session.step();
        for (unsigned i = 0; i < actors.size(); ++i) {
            const auto actor = actors[i];
            const auto ref = world.scene.reference(actor.get<PersistentEntityId>().value.str());
            const auto status = navigation->agent_state(ref);
            if (!status || status->phase == NavigationAgentPhase::Failed)
                throw std::runtime_error("Area Gallery Agent failed: " +
                                         runtime->debug(actor.id()).dump());
            const auto point = actor.get<LocalTranslation>();
            const auto side = planar ? point.y : point.z;
            if (planar)
                check(point.z == 0, "XY area policy changed the visual depth");
            if (i == 0 && std::abs(point.x) < 1.2)
                direct_crossed = true;
            if (i == 1)
                cost_extent = std::max(cost_extent, std::abs(side));
            if (i == 2) {
                excluded_extent = std::max(excluded_extent, std::abs(side));
                check(std::abs(point.x) >= 1.2 || std::abs(side) >= 2.9,
                      "Default-only Agent entered the classified crossing");
            }
            right[i] = right[i] || point.x > 3.5;
            returned[i] = returned[i] || (right[i] && point.x < -3.5);
            const auto filter = actor.get<NavigationAreaFilter>();
            check(filter.mask == (i == 2 ? 1u : 65535u) && filter.costs[2] == (i == 1 ? 20 : 1),
                  "Shared behavior overwrote an individual Agent's policy");
        }
        if (direct_crossed && cost_extent > 2.9 && excluded_extent > 2.9 &&
            std::ranges::all_of(returned, [](bool value) { return value; }))
            break;
    }
    check(direct_crossed && cost_extent > 2.9 && excluded_extent > 2.9 &&
              std::ranges::all_of(returned, [](bool value) { return value; }),
          "Gallery did not demonstrate direct, weighted and excluded looping routes");
    check(read(root / path) == saved, "Area Gallery runtime changed authored source");
}
void exercise_policies(const std::filesystem::path& root, const std::filesystem::path& path,
                       bool planar) {
    const auto saved = read(root / path);
    GameSessionConfig configuration;
    configuration.content_root = root;
    GameSession session(configuration);
    const auto candidate = session.prepare(saved);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!session.poll_preparation(candidate).ready) {
        check(std::chrono::steady_clock::now() < deadline, "Avoidance Gallery resources timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    session.activate(candidate, {}, false);
    auto& world = session.active();
    constexpr std::array policy_names{"Cyan - priority 0.8", "Orange - priority 0.2",
                                      "Violet - peer mask zero", "Gold - observes high bit"};
    std::array<flecs::entity, 4> actors;
    for (const auto& row : saved.at("entities"))
        for (unsigned i = 0; i < actors.size(); ++i)
            if (row.at("name").get<std::string>() == policy_names[i])
                actors[i] = world.scene.entity(row.at("id").get<std::string>());
    std::array<NavigationAvoidance, 4> policies;
    for (unsigned i = 0; i < actors.size(); ++i) {
        check(actors[i] && actors[i].has<NavigationAvoidance>() &&
                  actors[i].has<NavigationBehavior>(),
              "Avoidance Gallery lost editable policy or shared patrol behavior");
        policies[i] = actors[i].get<NavigationAvoidance>();
        check(actors[i].get<NavigationAgent>().navmesh == actors[0].get<NavigationAgent>().navmesh,
              "Avoidance Gallery duplicated its shared NavMesh");
    }
    check(policies[0].priority == .8f && policies[1].priority == .2f && policies[2].mask == 0 &&
              policies[3].mask == 0x80000000u && policies[2].layers == 0x80000000u &&
              policies[3].layers == 0x80000000u,
          "Avoidance Gallery lost its distinct priority and asymmetric high-bit examples");
    auto navigation =
        std::static_pointer_cast<NavigationRuntime>(world.engine.services().navigation());
    std::array<bool, 4> crossed{}, returned{};
    std::array<double, 2> minimum{100, 100}, deviation{};
    for (unsigned tick = 0; tick < 1000; ++tick) {
        session.step();
        std::array<LocalTranslation, 4> positions;
        for (unsigned i = 0; i < actors.size(); ++i) {
            const auto actor = actors[i];
            const auto status = navigation->agent_state(
                world.scene.reference(actor.get<PersistentEntityId>().value.str()));
            if (!status || status->phase == NavigationAgentPhase::Failed)
                throw std::runtime_error("Avoidance Gallery agent failed: " +
                                         navigation->debug(actor.id()).dump());
            positions[i] = actor.get<LocalTranslation>();
            const auto p = positions[i];
            const auto side = planar ? p.y : p.z;
            const auto lane = i < 2 ? 2.5 : -2.5;
            if (i % 2 == 0)
                check(std::abs(side - lane) < .001,
                      "Unobserved peer changed dominant Gallery steering");
            else
                deviation[i / 2] = std::max(deviation[i / 2], std::abs(side - lane));
            if (planar)
                check(p.z == 0, "XY avoidance changed visual depth");
            crossed[i] = crossed[i] || (i % 2 == 0 ? p.x > 3.5 : p.x < -3.5);
            returned[i] = returned[i] || (crossed[i] && (i % 2 == 0 ? p.x < -3.5 : p.x > 3.5));
            check(actor.get<NavigationAvoidance>() == policies[i],
                  "Shared patrol behavior overwrote individual avoidance policy");
        }
        for (unsigned lane = 0; lane < 2; ++lane) {
            const auto a = positions[lane * 2], b = positions[lane * 2 + 1];
            minimum[lane] =
                std::min(minimum[lane], std::hypot(a.x - b.x, planar ? a.y - b.y : a.z - b.z));
        }
        if (std::ranges::all_of(returned, [](bool value) { return value; }))
            break;
    }
    check(std::ranges::all_of(returned, [](bool value) { return value; }) && minimum[0] > .65 &&
              minimum[1] > .65 && deviation[0] > .2 && deviation[1] > .2,
          "Gallery did not demonstrate both looping asymmetric yielding policies");
    check(read(root / path) == saved, "Avoidance Gallery runtime changed authored source");
}
} // namespace
int main(int argc, char** argv) {
    try {
        const bool peers = argc >= 2 && (std::string_view(argv[1]) == "--prepare-policies" ||
                                         std::string_view(argv[1]) == "--policies");
        if (argc == 4 && (std::string_view(argv[1]) == "--prepare" ||
                          std::string_view(argv[1]) == "--prepare-policies")) {
            prepare(std::filesystem::absolute(argv[2]), std::filesystem::absolute(argv[3]), peers);
            if (!peers)
                prepare(std::filesystem::absolute(argv[2]), std::filesystem::absolute(argv[3]),
                        true);
            return 0;
        }
        check(argc == (peers ? 4 : 3), "Need Gallery and export scratch parent");
        const auto& paths = peers ? policy_paths : area_paths;
        const auto run = peers ? exercise_policies : exercise;
        const auto scratch =
            std::filesystem::absolute(argv[peers ? 3 : 2]) / AssetId::generate().str();
        const auto source = scratch / "Gallery";
        std::filesystem::create_directories(source);
        std::filesystem::copy(argv[peers ? 2 : 1], source,
                              std::filesystem::copy_options::recursive);
        std::array<AssetId, 2> roots;
        for (unsigned i = 0; i < paths.size(); ++i) {
            roots[i] = read(source / paths[i]).at("asset_id").get<AssetId>();
            run(source, paths[i], bool(i));
        }
        const auto output = scratch / "Export", relocated = scratch / "Relocated";
        const RuntimePackageTarget target{"windows", "d3d12"};
        package_runtime_content(source, output, roots, target);
        std::filesystem::rename(output, relocated);
        std::filesystem::rename(source, scratch / "SourcesOffline");
        const auto catalog = open_runtime_content(relocated, target);
        for (unsigned i = 0; i < roots.size(); ++i)
            run(relocated, catalog.records().at(roots[i]).source, bool(i));
        std::cout << "Both Gallery scenes and source-hidden relocated export passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
