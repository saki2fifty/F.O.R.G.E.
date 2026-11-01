#include "runtime_package.hpp"
#include "transform_animation_authoring.hpp"
#include <chrono>
#include <cmath>
#include <forge/animation.hpp>
#include <forge/navigation.hpp>
#include <forge/navigation_build.hpp>
#include <forge/render_scene.hpp>
#include <forge/runtime.hpp>
#include <forge/runtime_resources.hpp>
#include <fstream>
#include <iostream>
#include <thread>
using namespace forge;
namespace {
constexpr const char* scenes[]{"Scenes/129-moving-navigation-3d.scene.json",
                               "Scenes/130-moving-navigation-xy.scene.json"};
constexpr const char* clips[]{"Assets/Navigation/MovingObstacle3D.animation.json",
                              "Assets/Navigation/MovingObstacleXY.animation.json"};
void check(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
Json read(const std::filesystem::path& path) {
    Json value;
    std::ifstream(path) >> value;
    return value;
}
void prepare(const std::filesystem::path& root, const std::filesystem::path& worker,
             unsigned first = 0) {
    auto lease = std::make_shared<ProjectLease>(root);
    AssetImportService imports(lease, transform_animation_import_registry(),
                               {"windows", "d3d12", "desktop"});
    for (unsigned index = first; index < 2; ++index) {
        const auto clip = read(root / clips[index]);
        const auto id = clip.at("asset_id").get<AssetId>();
        imports.submit(
            imports.prepare(clips[index], {}, id),
            [](auto& candidate, const auto& plan, const auto&) {
                prepare_transform_animation_publication(candidate, plan);
            },
            [](const auto&, const auto&) {});
        check(imports.wait_idle(std::chrono::seconds(10)), "Gallery clip publication timed out");
        const auto receipts = imports.poll();
        check(receipts.size() == 1 && receipts.front().published,
              receipts.empty() ? "Missing Gallery clip receipt" : receipts.front().diagnostic);
        EngineContext engine;
        Scene scene(engine.world());
        scene.restore_snapshot(read(root / scenes[index]));
        NavigationSettings settings;
        settings.plane = index ? NavigationPlane::PlanarXY : NavigationPlane::Spatial3D;
        settings.dynamic_obstacles = true;
        const auto record = prepare_navigation(root, scene.effective_document(), settings, worker)
                                .publish(scene.effective_document());
        engine.world().world().each([&](flecs::entity e, const NavigationAgent& agent) {
            auto assigned = agent;
            assigned.navmesh = {record.id};
            e.set<NavigationAgent>(assigned);
        });
        std::ofstream out(root / scenes[index]);
        out << scene.snapshot().dump(2) << '\n';
        check(bool(out.flush()), "Cannot save prepared Gallery Scene");
        std::cout << scenes[index] << " NavMesh=" << record.id.str() << '\n';
    }
}
void exercise(const std::filesystem::path& root, const std::filesystem::path& scene_path,
              bool planar, bool cooked) {
    const auto source = read(root / scene_path);
    auto modules = std::vector<EngineModule>{animation_module(root), navigation_module(root)};
    if (cooked)
        modules.push_back(runtime_resources_module(root));
    EngineContext engine(WorldRole::Runtime, false, std::move(modules));
    Scene scene(engine.world());
    scene.restore_snapshot(source);
    RuntimeSimulation simulation(engine.world(), scene);
    const auto animation = animation_runtime(engine.world());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!animation->prepare_initial_pose()) {
        check(std::chrono::steady_clock::now() < deadline, "Gallery initial animation timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    auto navigation = std::static_pointer_cast<NavigationRuntime>(engine.services().navigation());
    flecs::entity agent, obstacle;
    for (const auto& row : source.at("entities")) {
        if (row.at("components").contains("forge.navigation_agent"))
            agent = scene.entity(row.at("id").get<std::string>());
        if (row.at("components").contains("forge.navigation_obstacle"))
            obstacle = scene.entity(row.at("id").get<std::string>());
    }
    check(agent && obstacle && obstacle.has<TransformAnimator>(), "Gallery participants missing");
    const auto reference = engine.world().reference(agent.id()).value();
    const auto original = source;
    bool saw_detour = false, saw_direct = false;
    double minimum_offset = 100, maximum_offset = -100;
    for (unsigned tick = 1; tick <= 960; ++tick) {
        engine.world().evaluate_world_transforms();
        const auto before = agent.get<WorldTransform>().affine.point({0, 0, 0});
        simulation.tick(1.f / 60);
        const auto state = navigation->agent_state(reference).value();
        check(state.sampled && state.has_path && state.status == NavStatus::Success &&
                  state.tick == tick,
              "Moving obstacle Gallery lost a valid agent sample: " +
                  navigation->debug(agent.id()).dump());
        check(std::hypot(state.position[0] - before[0], state.position[1] - before[1],
                         state.position[2] - before[2]) <= 1.8 / 60 + .00001,
              "Moving obstacle Gallery exceeded its speed budget");
        if (planar)
            check(state.position[2] == -1 && state.velocity[2] == 0,
                  "Moving XY obstacle changed agent visual depth");
        const auto path = navigation->debug(agent.id()).at("path");
        if (tick < 30)
            saw_detour |= path.size() > 2;
        if (tick > 120 && tick < 240)
            saw_direct |= path.size() == 2;
        const auto center = obstacle.get<WorldTransform>().affine.point({0, 0, 0});
        const auto offset = center[planar ? 1 : 2];
        minimum_offset = std::min(minimum_offset, offset);
        maximum_offset = std::max(maximum_offset, offset);
        if (tick == 1 || tick == 240 || tick == 960) {
            const auto rendered = extract_render_scene(simulation.presentation(.5));
            check(rendered.cameras.size() == 1 && rendered.lights.size() == 1 &&
                      rendered.meshes.size() >= 4,
                  "Gallery lost its ordinary render submissions");
        }
    }
    check(saw_detour && saw_direct && minimum_offset < -3.9 && maximum_offset > 3.9 &&
              navigation->agent_state(reference)->phase == NavigationAgentPhase::Arrived &&
              read(root / scene_path) == original,
          "Gallery failed to show animated route changes/arrival or rewrote authored content");
}
} // namespace
int main(int argc, char** argv) {
    try {
        if ((argc == 4 || (argc == 5 && std::string_view(argv[4]) == "--xy-only")) &&
            std::string_view(argv[1]) == "--prepare") {
            prepare(std::filesystem::absolute(argv[2]), std::filesystem::absolute(argv[3]),
                    argc == 5 ? 1u : 0u);
            return 0;
        }
        check(argc == 3, "Need Gallery project and scratch root");
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto root = scratch / "Gallery";
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        for (unsigned index = 0; index < 2; ++index) {
            exercise(root, scenes[index], index != 0, false);
            const auto source = read(root / scenes[index]);
            const std::array roots{source.at("asset_id").get<AssetId>()};
            const auto game = scratch / (index ? "XYGame" : "SpatialGame");
            (void)package_runtime_content(root, game, roots, {"windows", "d3d12"});
            const auto moved = scratch / (index ? "RelocatedXY" : "RelocatedSpatial");
            std::filesystem::rename(game, moved);
            const auto hidden = scratch / "SourcesOffline";
            std::filesystem::rename(root, hidden);
            const auto catalog = open_runtime_content(moved, {"windows", "d3d12"});
            check(catalog.records().size() == 3,
                  "Export lost Scene/nav/clip closure or kept sources");
            const auto shipped_scene = catalog.records().at(roots[0]).source;
            check(!std::filesystem::exists(moved / clips[index]),
                  "Export retained editable clip source");
            exercise(moved, shipped_scene, index != 0, true);
            std::filesystem::rename(hidden, root);
        }
        std::filesystem::remove_all(scratch);
        std::cout << "Gallery129/130 animated obstacle routes, arrival, ordinary render submission "
                     "and relocated source-offline package PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
