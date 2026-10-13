#include "runtime_package.hpp"
#include <forge/navigation.hpp>
#include <forge/render_scene.hpp>
#include <forge/runtime.hpp>
#include <forge/runtime_resources.hpp>
#include <fstream>
#include <iostream>
#include <thread>
using namespace forge;
namespace {
void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
Json read(const std::filesystem::path& path) {
    Json value;
    std::ifstream(path) >> value;
    return value;
}
void exercise(const std::filesystem::path& root, const Json& source,
              const std::filesystem::path& saved_path, bool cooked) {
    auto modules = std::vector<EngineModule>{navigation_module(root)};
    if (cooked)
        modules.push_back(runtime_resources_module(root));
    EngineContext engine(WorldRole::Runtime, false, std::move(modules));
    Scene scene(engine.world());
    scene.restore_snapshot(source);
    RuntimeSimulation simulation(engine.world(), scene);
    auto navigation = engine.world().services().navigation();
    flecs::entity agent;
    for (const auto& row : source.at("entities"))
        if (row.at("components").contains("forge.navigation_agent"))
            agent = scene.entity(row.at("id"));
    check(agent && agent.get<NavigationAgent>().enabled, "Gallery50 lacks an active agent");
    const auto reference = engine.world().reference(agent.id()).value();
    const auto saved = read(root / saved_path);
    check(navigation->agent_state(reference)->phase == NavigationAgentPhase::Pending,
          "Gallery50 started with an old sample");
    if (cooked) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        for (;;) {
            engine.services().resources()->synchronize();
            const auto path = navigation->find_path(agent.get<NavigationAgent>().navmesh,
                                                    {-8, .1, 0}, {8, .1, 0});
            if (path.status == NavStatus::Success)
                break;
            check(path.status == NavStatus::Unavailable,
                  "Cooked Gallery navigation failed admission");
            check(std::chrono::steady_clock::now() < deadline, "Gallery Mesh load timed out");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    bool around = false;
    for (unsigned tick = 1; tick <= 660; ++tick) {
        simulation.tick(1.f / 60);
        const auto state = navigation->agent_state(reference).value();
        check(state.sampled && state.has_path && state.status == NavStatus::Success &&
                  state.tick == tick,
              "Gallery50 lost a completed movement sample");
        check(state.phase == NavigationAgentPhase::Moving ||
                  state.phase == NavigationAgentPhase::Arrived,
              "Gallery50 route stalled or failed");
        check(!(std::abs(state.position[0]) < 1 && std::abs(state.position[2]) < 2),
              "Gallery50 entered the obstacle footprint");
        if (std::abs(state.position[0]) < .8) {
            check(std::abs(state.position[2]) > 2, "Gallery50 crossed the baked obstacle");
            around = true;
        }
        if (tick == 1 || tick == 300 || tick == 660) {
            const auto presentation = simulation.presentation(.5);
            const auto render = extract_render_scene(presentation);
            check(render.cameras.size() == 1 && render.lights.size() == 1 &&
                      render.meshes.size() == 3,
                  "Gallery50 renderer did not admit camera/light/three shapes");
            bool observed = false;
            for (const auto& row : presentation.at("entities"))
                if (row.at("id") == reference.entity.str())
                    observed = row.at("navigation_debug").at("tick") == tick;
            check(observed, "Gallery50 presentation lost the agent observation");
        }
    }
    const auto final = navigation->agent_state(reference).value();
    check(around && final.phase == NavigationAgentPhase::Arrived &&
              final.remaining_distance <= .101 && final.velocity == Double3{},
          "Gallery50 did not arrive and stop");
    check(read(root / saved_path) == saved, "Gameplay rewrote saved Gallery50");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3 || (argc == 4 && std::string_view(argv[3]) == "--static-mesh"),
              "Need Gallery project and scratch root");
        const bool cooked = argc == 4;
        const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        const auto scene_path =
            std::filesystem::path(cooked ? "Scenes/51-static-mesh-navigation.scene.json"
                                         : "Scenes/50-navigation-progress.scene.json");
        const auto source = read(root / scene_path);
        exercise(root, source, scene_path, cooked);
        const auto game = root.string() + "-game";
        const auto moved = game + "-relocated";
        const std::array roots{source.at("asset_id").get<AssetId>()};
        (void)package_runtime_content(root, game, roots, {"windows", "d3d12"});
        std::filesystem::rename(game, moved);
        const auto hidden = root.string() + "-source-offline";
        std::filesystem::rename(root, hidden);
        const auto catalog = open_runtime_content(moved, {"windows", "d3d12"});
        check(catalog.records().size() == (cooked ? 3u : 2u),
              "Gallery50 export retained unrelated content or lost navmesh");
        const auto shipped_path = catalog.records().at(roots[0]).source;
        exercise(moved, read(std::filesystem::path(moved) / shipped_path), shipped_path, cooked);
        std::filesystem::remove_all(hidden);
        std::filesystem::remove_all(moved);
        std::cout << "Gallery50 actual navigation observations, obstacle/arrival, render and "
                     "relocated source-offline export passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
