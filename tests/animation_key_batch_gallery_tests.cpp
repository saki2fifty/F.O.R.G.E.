#include "runtime_package.hpp"
#include <array>
#include <cmath>
#include <forge/animation.hpp>
#include <forge/authoring.hpp>
#include <forge/render_scene.hpp>
#include <forge/runtime.hpp>
#include <forge/transform_animation.hpp>
#include <fstream>
#include <iostream>
#include <set>
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
    require(std::abs(actual - expected) < .00005, "Gallery batch analytic motion differs");
}
double ease(double t) {
    const auto u = std::clamp(t / 2., 0., 1.);
    return u * u * (3 - 2 * u);
}
double phase(unsigned lane, double t) {
    if (lane == 0)
        return ease(t);
    if (lane == 1)
        return ease(t - 1);
    if (t <= 2)
        return ease(t);
    if (t < 3) {
        const auto u = t - 2;
        return 1 - u * u * (3 - 2 * u);
    }
    return ease(t - 3);
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
                    "Batch Gallery resource admission stalled");
            std::this_thread::sleep_for(1ms);
        }
        simulation.reset_presentation();
    }
};
void inspect(World& world, const std::array<EntityId, 3>& actors, double t) {
    t = std::fmod(t, 6.);
    for (unsigned lane = 0; lane < 3; ++lane) {
        const auto actor = world.scene.entity(actors[lane].str());
        const auto p = phase(lane, t);
        near(actor.get<LocalTranslation>().x, -3 + 6 * p);
        near(actor.get<LocalTranslation>().y, 1.5);
        near(actor.get<LocalScale>().y, .4 + .4 * p);
        // Rotation retains the existing shortest-arc slerp, independent of cubic vectors.
        const auto raw = lane == 1                     ? std::clamp((t - 1) / 2., 0., 1.)
                         : lane == 2 && t > 2 && t < 3 ? 3 - t
                         : lane == 2 && t >= 3         ? std::clamp((t - 3) / 2., 0., 1.)
                                                       : std::clamp(t / 2., 0., 1.);
        const auto rotation = rotation_from_euler({0, 180 * raw, 0});
        const auto actual = actor.get<LocalRotation>();
        near(std::abs(actual.y), std::abs(rotation.y));
        near(std::abs(actual.w), std::abs(rotation.w));
    }
    const auto render = extract_render_scene(world.simulation.presentation(1));
    if (render.cameras.empty() || render.meshes.size() != 10 || !render.diagnostics.empty()) {
        std::cerr << "cameras=" << render.cameras.size() << " meshes=" << render.meshes.size()
                  << " diagnostics=" << render.diagnostics.size() << '\n';
        for (const auto& diagnostic : render.diagnostics)
            std::cerr << diagnostic.text << '\n';
        require(false, "Batch Gallery camera, primitive meshes or render admission differs");
    }
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Need Gallery and scratch root");
        const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        const auto source = read(root / "Scenes/18-animation-key-batches.scene.json");
        const auto catalog = AssetCatalog::open_project(root);
        const std::array<const char*, 3> names{"BatchOriginal.animation.json",
                                               "BatchShifted.animation.json",
                                               "BatchRepeated.animation.json"};
        std::array<TransformAnimationSource, 3> authored;
        std::array<AssetId, 3> clips{};
        std::array<EntityId, 3> actors{};
        for (unsigned i = 0; i < 3; ++i) {
            authored[i] = {read(root / "Assets" / names[i])};
            authored[i].validate();
            clips[i] = authored[i].asset();
            const auto& selected = catalog.records().at(clips[i]).metadata.at("forge.import");
            require(selected.at("platform") == "windows" && selected.at("backend") == "d3d12" &&
                        selected.at("output_version") == 2,
                    "Wrong selected batch recipe/target");
            for (const auto& row : source.at("entities"))
                if (row.at("components").contains("forge.transform_animator") &&
                    row.at("components").at("forge.transform_animator").at("clip").get<AssetId>() ==
                        clips[i])
                    actors[i] = row.at("id").get<EntityId>();
            require(bool(actors[i]), "Batch Gallery actor missing");
        }
        std::set<AssetId> original_ids;
        for (const auto& key : authored[0].document.at("keys"))
            original_ids.insert(key.at("id").get<AssetId>());
        auto shifted = authored[0].document;
        shifted["asset_id"] = clips[1];
        for (auto& key : shifted.at("keys"))
            key["time_us"] = key.at("time_us").get<std::int64_t>() + 1000000;
        require(shifted == authored[1].document, "Shift changed complete key data or identities");
        require(authored[2].document.at("keys").size() == 4, "Gallery duplicate count differs");
        std::set<AssetId> copies;
        for (unsigned i = 0; i < 4; ++i) {
            auto actual = authored[2].document.at("keys")[i];
            auto expected = authored[0].document.at("keys")[i % 2];
            if (i >= 2) {
                const auto id = actual.at("id").get<AssetId>();
                require(!original_ids.contains(id) && copies.insert(id).second,
                        "Gallery duplicate identity recycled");
                actual["id"] = expected["id"];
                expected["time_us"] = expected.at("time_us").get<std::int64_t>() + 3000000;
            }
            require(actual == expected, "Duplicated Gallery metadata differs");
        }
        World live(root, source);
        inspect(live, actors, 0);
        for (unsigned step = 1; step <= 24; ++step) {
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
        require(shipped.records().size() == 4, "Batch Gallery selected unrelated dependencies");
        for (unsigned i = 0; i < 3; ++i)
            require(shipped.records().contains(clips[i]) &&
                        !std::filesystem::exists(moved / "Assets" / names[i]),
                    "Batch selected cook missing or source leaked");
        std::filesystem::rename(root / "Assets", root / "source-offline");
        World game(moved, source);
        inspect(game, actors, 0);
        for (unsigned step = 1; step <= 24; ++step) {
            game.simulation.tick(.25f);
            inspect(game, actors, step * .25);
        }
        std::cout
            << "Gallery original/shifted/duplicated metadata, selected resource, independent "
               "analytic cubic/slerp motion, camera and relocated source-free export passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
