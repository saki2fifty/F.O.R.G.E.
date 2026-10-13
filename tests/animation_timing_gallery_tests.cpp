#include "runtime_package.hpp"
#include <forge/animation.hpp>
#include <forge/authoring.hpp>
#include <forge/render_scene.hpp>
#include <forge/runtime.hpp>
#include <forge/transform_animation.hpp>
#include <fstream>
#include <iostream>
#include <thread>
using namespace forge;
using namespace std::chrono_literals;
namespace {
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
Json read(const std::filesystem::path& path) {
    Json value;
    std::ifstream(path) >> value;
    return value;
}
void near(double actual, double expected) {
    require(std::abs(actual - expected) < .00005, "Gallery timing analytic pose differs");
}
double position(unsigned lane, double time) {
    const double duration = lane == 1 ? 8 : 4;
    time = std::fmod(time, duration);
    if (lane == 1)
        time /= 2;
    const std::array<double, 5> times{0, lane == 2 ? .5 : .57, 2, lane == 2 ? 2.5 : 2.57, 4};
    const std::array<double, 5> values{-3, -1.5, 3, 1.5, -3};
    for (unsigned i = 1; i < times.size(); ++i)
        if (time <= times[i]) {
            const auto u = (time - times[i - 1]) / (times[i] - times[i - 1]);
            return values[i - 1] + (values[i] - values[i - 1]) * u * u * (3 - 2 * u);
        }
    throw std::runtime_error("Invalid independent Gallery time");
}
struct World {
    EngineContext engine;
    Scene scene;
    RuntimeSimulation simulation;
    std::shared_ptr<AnimationRuntime> animation;
    World(const std::filesystem::path& root, const Json& source)
        : engine(WorldRole::Runtime, false, {animation_module(root)}), scene(engine.world()),
          simulation(engine.world(), scene), animation(animation_runtime(engine.world())) {
        scene.restore_snapshot(source);
        const auto deadline = std::chrono::steady_clock::now() + 20s;
        while (!animation->prepare_initial_pose()) {
            require(std::chrono::steady_clock::now() < deadline,
                    "Timing Gallery resource admission stalled");
            std::this_thread::sleep_for(1ms);
        }
        simulation.reset_presentation();
    }
};
void inspect(World& world, const std::array<EntityId, 3>& actors, double time) {
    for (unsigned lane = 0; lane < actors.size(); ++lane) {
        const auto entity = world.scene.entity(actors[lane].str());
        near(entity.get<LocalTranslation>().x, position(lane, time));
        near(entity.get<LocalTranslation>().y, 1.5);
        near(entity.get<LocalTranslation>().z, 0);
        near(entity.get<LocalScale>().x, 2);
        near(entity.get<LocalScale>().y, .4);
        near(entity.get<LocalScale>().z, 1);
        near(entity.get<LocalRotation>().w, 1);
    }
    const auto render = extract_render_scene(world.simulation.presentation(1));
    if (render.cameras.empty() || render.meshes.size() != 10 || !render.diagnostics.empty()) {
        std::cerr << "cameras=" << render.cameras.size() << " meshes=" << render.meshes.size()
                  << " diagnostics=" << render.diagnostics.size() << '\n';
        for (const auto& diagnostic : render.diagnostics)
            std::cerr << diagnostic.text << '\n';
    }
    require(!render.cameras.empty() && render.meshes.size() == 10 && render.diagnostics.empty(),
            "Timing Gallery camera, meshes or render admission differs");
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Need Gallery and scratch root");
        const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        const auto source = read(root / "Scenes/45-animation-timing.scene.json");
        const auto catalog = AssetCatalog::open_project(root);
        const std::array<const char*, 3> names{"OriginalTiming", "RetimedTiming",
                                               "FrameGridTiming"};
        std::array<TransformAnimationSource, 3> authored;
        std::array<AssetId, 3> clips{};
        std::array<EntityId, 3> actors{};
        for (unsigned i = 0; i < names.size(); ++i) {
            const auto path = std::filesystem::path("Assets/AnimationTiming") /
                              (std::string(names[i]) + ".animation.json");
            authored[i] = {read(root / path)};
            authored[i].validate();
            clips[i] = authored[i].asset();
            const auto& selected = catalog.records().at(clips[i]).metadata.at("forge.import");
            require(selected.at("platform") == "windows" && selected.at("backend") == "d3d12" &&
                        selected.at("output_version") == 2,
                    "Wrong selected timing recipe/target");
            for (const auto& row : source.at("entities"))
                if (row.at("components").contains("forge.transform_animator") &&
                    row.at("components").at("forge.transform_animator").at("clip").get<AssetId>() ==
                        clips[i])
                    actors[i] = row.at("id").get<EntityId>();
            require(bool(actors[i]), "Timing Gallery actor missing");
        }
        auto retimed = authored[0];
        retimed.document["asset_id"] = clips[1];
        retimed.retime_duration(8000000);
        require(retimed.document == authored[1].document, "Retimed Gallery data differs");
        auto snapped = authored[0];
        snapped.document["asset_id"] = clips[2];
        std::vector<AssetId> keys;
        for (const auto& key : snapped.clip().keys)
            keys.push_back(key.id);
        snapped.snap_keys_to_frames(keys, 2);
        require(snapped.document == authored[2].document, "Frame-grid Gallery data differs");
        World live(root, source);
        inspect(live, actors, 0);
        for (unsigned step = 1; step <= 32; ++step) {
            live.simulation.tick(.25f);
            inspect(live, actors, step * .25);
        }
        const std::array roots{source.at("asset_id").get<AssetId>()};
        auto lease = std::make_shared<ProjectLease>(root);
        prepare_runtime_content_catalog(*lease, roots, {});
        const auto package = root.parent_path() / (root.filename().string() + "-export");
        (void)package_runtime_content(root, package, roots, {"windows", "d3d12"});
        const auto moved = std::filesystem::path(package.string() + "-relocated");
        std::filesystem::rename(package, moved);
        const auto shipped = open_runtime_content(moved, {"windows", "d3d12"});
        require(shipped.records().size() == 4, "Timing Gallery selected unrelated dependencies");
        for (unsigned i = 0; i < names.size(); ++i)
            require(shipped.records().contains(clips[i]) &&
                        !std::filesystem::exists(moved / "Assets/AnimationTiming" /
                                                 (std::string(names[i]) + ".animation.json")),
                    "Timing selected cook missing or source leaked");
        std::filesystem::rename(root / "Assets", root / "source-offline");
        World game(moved, source);
        inspect(game, actors, 0);
        for (unsigned step = 1; step <= 32; ++step) {
            game.simulation.tick(.25f);
            inspect(game, actors, step * .25);
        }
        std::cout << "Gallery timing source/IDs, selected cooks, independent analytic motion, "
                     "render admission and relocated source-free export passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
