#include "navigation_rebuild_access.hpp"
#include <forge/authoring.hpp>
#include <forge/navigation.hpp>
#include <forge/navigation_build.hpp>
#include <forge/physics_components.hpp>
#include <forge/runtime.hpp>
#include <fstream>
#include <iostream>
#include <thread>

using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F&& f) {
    try {
        f();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Expected runtime navigation rejection");
}
std::string read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
struct Live {
    EngineContext engine;
    Scene scene;
    RuntimeSimulation simulation;
    std::shared_ptr<NavigationRuntime> navigation;
    Live(const std::filesystem::path& root, const std::filesystem::path& worker, const Json& source)
        : engine(WorldRole::Runtime, false, {navigation_module(root, worker)}),
          scene(engine.world()), simulation(engine.world(), scene),
          navigation(std::static_pointer_cast<NavigationRuntime>(engine.services().navigation())) {
        scene.restore_snapshot(source);
        simulation.tick(1.f / 60);
    }
};
NavigationRebuildObservation wait(Live& live, std::uint64_t token) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    for (;;) {
        live.navigation->poll_rebuild();
        const auto state = live.navigation->rebuild_state(token);
        if (state.phase >= NavigationRebuildPhase::Applied)
            return state;
        check(std::chrono::steady_clock::now() < deadline, "Runtime navigation rebuild timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
void run(const std::filesystem::path& root, const std::filesystem::path& worker, bool planar,
         bool dynamic) {
    std::filesystem::create_directories(root);
    EngineContext author;
    Scene scene(author.world());
    const auto floor_id =
        authoring_command(scene, "entity.create",
                          {{"name", "Runtime walkable floor"}, {"recipe", "primitive.3"}})
            .at("selected")
            .get<std::string>();
    if (planar)
        scene.entity(floor_id).set<NavigationRegion2D>({true, false, 20, 20});
    else
        scene.entity(floor_id).set<LocalScale>({20, 1, 20}).set<NavigationSurface>({true});
    const auto effective = scene.effective_document();
    NavigationSettings settings;
    settings.plane = planar ? NavigationPlane::PlanarXY : NavigationPlane::Spatial3D;
    settings.dynamic_obstacles = dynamic;
    const auto asset = prepare_navigation(root, effective, settings, worker).publish(effective);
    const auto index_before = read(AssetCatalog::project_index(root));
    const auto artifact_before = read(root / asset.source);
    const auto snapshot = scene.snapshot();
    Live first(root, worker, snapshot), second(root, worker, snapshot),
        disabled(root, {}, snapshot);
    rejects([&] { disabled.navigation->request_rebuild({asset.id}); });
    check(first.navigation->find_path({asset.id}, {-6, 0, 0}, {6, 0, 0}).status ==
              NavStatus::Success,
          "Original navigation basis is unavailable");
    first.scene.entity(floor_id).set<LocalTranslation>({40, 0, 0});
    first.simulation.tick(1.f / 60);
    check(first.navigation->find_path({asset.id}, {34, 0, 0}, {46, 0, 0}).status ==
              NavStatus::Stale,
          "Changed source exposed an outdated navigation basis");
    const auto token = first.navigation->request_rebuild({asset.id});
    check(first.navigation->checkpoint().is_null(), "Pending work admitted partial recovery");
    rejects([&] {
        NavigationRebuildAccess::control(
            *first.navigation,
            {{"generation", std::uint64_t(2)}, {"operation", "cancel"}, {"token", token}}, 1);
    });
    check(first.navigation->rebuild_state(token).phase == NavigationRebuildPhase::Queued,
          "Request performed hidden geometry capture or worker adoption");
    rejects([&] { first.navigation->request_rebuild({asset.id}); });
    rejects([&] { second.navigation->rebuild_state(token); });
    const auto rebuilt = wait(first, token);
    check(first.navigation->checkpoint().is_null(),
          "Private rebuilt basis claimed catalog recovery");
    check(rebuilt.phase == NavigationRebuildPhase::Applied && rebuilt.query_generation > 0 &&
              rebuilt.geometry_revision.size() == 64 &&
              first.navigation->find_path({asset.id}, {34, 0, 0}, {46, 0, 0}).status ==
                  NavStatus::Success &&
              second.navigation->find_path({asset.id}, {-6, 0, 0}, {6, 0, 0}).status ==
                  NavStatus::Success,
          "World-private rebuild did not replace the changed basis independently");
    check(!first.navigation->cancel_rebuild(token), "Completed rebuild accepted cancellation");
    const auto observed = NavigationRebuildAccess::state(*first.navigation);
    check(observed.at("phase") == "Applied" && observed.at("asset").get<AssetId>() == asset.id,
          "Shared Play observation disagrees with the runtime owner");

    const auto stale = first.navigation->request_rebuild({asset.id});
    rejects([&] { first.navigation->rebuild_state(token); });
    first.navigation->poll_rebuild();
    check(first.navigation->rebuild_state(stale).phase == NavigationRebuildPhase::Building,
          "Writable polling did not launch the detached worker");
    first.scene.entity(floor_id).set<LocalTranslation>({80, 0, 0});
    first.navigation->poll_rebuild();
    check(first.navigation->rebuild_state(stale).phase == NavigationRebuildPhase::Stale &&
              first.navigation->find_path({asset.id}, {74, 0, 0}, {86, 0, 0}).status ==
                  NavStatus::Stale,
          "A superseded geometry result was exposed");
    // Retire the cancelled worker before accepting another request.
    std::uint64_t cancelled = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!cancelled) {
        first.navigation->poll_rebuild();
        try {
            cancelled = first.navigation->request_rebuild({asset.id});
        } catch (const std::exception&) {
            check(std::chrono::steady_clock::now() < deadline, "Stale worker did not retire");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    check(first.navigation->cancel_rebuild(cancelled) &&
              !first.navigation->cancel_rebuild(cancelled) &&
              first.navigation->rebuild_state(cancelled).phase == NavigationRebuildPhase::Cancelled,
          "Queued cancellation did not retain its terminal observation");
    const auto final = first.navigation->request_rebuild({asset.id});
    check(wait(first, final).phase == NavigationRebuildPhase::Applied &&
              first.navigation->find_path({asset.id}, {74, 0, 0}, {86, 0, 0}).status ==
                  NavStatus::Success,
          "A later valid rebuild did not recover after stale work and cancellation");
    check(read(AssetCatalog::project_index(root)) == index_before &&
              read(root / asset.source) == artifact_before,
          "Runtime rebuilding wrote authored navigation bytes or the catalog");
    PhysicsBody moving;
    moving.motion = 1;
    first.scene.entity(floor_id).set(moving);
    const auto invalid = first.navigation->request_rebuild({asset.id});
    const auto refused = wait(first, invalid);
    check(refused.phase != NavigationRebuildPhase::Applied &&
              refused.diagnostic.find("static") != std::string::npos,
          "Runtime rebuilding broadened source admission to a moving physics body");
    first.scene.entity(floor_id).remove<PhysicsBody>();
    first.simulation.tick(1.f / 60);
    check(first.navigation->find_path({asset.id}, {74, 0, 0}, {86, 0, 0}).status ==
              NavStatus::Success,
          "Invalid candidate destroyed the last valid private basis");
    const auto obstacle_id =
        authoring_command(first.scene, "entity.create", {{"kind", 4}, {"name", "Current obstacle"}})
            .at("selected")
            .get<std::string>();
    first.scene.entity(obstacle_id)
        .set<LocalTranslation>({80, 0, 0})
        .set<NavigationObstacle>({true, 1.1f, 2});
    first.simulation.tick(1.f / 60);
    const auto with_obstacle = first.navigation->request_rebuild({asset.id});
    const auto carved = wait(first, with_obstacle);
    if (dynamic) {
        const auto path = first.navigation->find_path({asset.id}, {74, 0, 0}, {86, 0, 0});
        check(carved.phase == NavigationRebuildPhase::Applied &&
                  path.status == NavStatus::Success && path.points.size() > 2,
              "Adopted navigation basis exposed a route before current obstacle carving");
    } else
        check(carved.phase == NavigationRebuildPhase::Failed,
              "Static rebuild silently admitted enabled dynamic obstacle state");
    first.scene.entity(obstacle_id).set<NavigationObstacle>({false, 1.1f, 2});
    first.simulation.tick(1.f / 60);
    check(first.navigation->find_path({asset.id}, {74, 0, 0}, {86, 0, 0}).status ==
              NavStatus::Success,
          "Rejected obstacle adoption retired the valid basis or prevented later restoration");
    rejects([&] { first.navigation->rebuild_state(0); });
    const auto pending = first.navigation->request_rebuild({asset.id});
    first.navigation->poll_rebuild();
    first.navigation->bind(&first.scene);
    rejects([&] { first.navigation->rebuild_state(pending); });
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need test namespace and matching navigation worker");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        try {
            for (bool planar : {false, true})
                for (bool dynamic : {false, true})
                    run(root / ((planar ? "planar" : "spatial") +
                                std::string(dynamic ? "-tiles" : "-static")),
                        std::filesystem::absolute(argv[2]), planar, dynamic);
        } catch (...) {
            std::filesystem::remove_all(root);
            throw;
        }
        std::filesystem::remove_all(root);
        std::cout << "Runtime navigation rebuild checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
