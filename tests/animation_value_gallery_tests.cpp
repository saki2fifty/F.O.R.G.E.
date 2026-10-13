#include "asset_bytes.hpp"
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
    require(std::abs(actual - expected) < .00005, "Gallery value analytic pose differs");
}
double fraction(double time) {
    time = std::fmod(time, 4.);
    const double u = time <= 2 ? time / 2 : (4 - time) / 2;
    return u * u * (3 - 2 * u);
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
                    "Values Gallery resource admission stalled");
            std::this_thread::sleep_for(1ms);
        }
        simulation.reset_presentation();
    }
};
void inspect(World& world, const std::array<EntityId, 3>& actors, double time) {
    for (unsigned lane = 0; lane < actors.size(); ++lane) {
        const auto entity = world.scene.entity(actors[lane].str());
        const auto f = fraction(time);
        const double x = -3 + 6 * f, y = 1.5 + f;
        near(entity.get<LocalTranslation>().x, lane == 1 ? -.5 * x + 2 : x);
        near(entity.get<LocalTranslation>().y, lane == 1 ? 1.3 * y + .3 : y);
        near(entity.get<LocalTranslation>().z, 0);
        near(entity.get<LocalScale>().x, (2 - f) * (lane == 2 ? 2 : 1));
        near(entity.get<LocalScale>().y, (.4 + .8 * f) * (lane == 2 ? .5 : 1));
        near(entity.get<LocalScale>().z, (1 + .5 * f) * (lane == 2 ? 1.5 : 1));
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
            "Values Gallery camera, meshes or render admission differs");
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Need Gallery and scratch root");
        const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        const auto source = read(root / "Scenes/47-animation-key-values.scene.json");
        const auto catalog = AssetCatalog::open_project(root);
        const std::array<const char*, 3> names{"OriginalValues", "PositionValues", "ScaleValues"};
        std::array<TransformAnimationSource, 3> authored;
        std::array<AssetId, 3> clips{};
        std::array<EntityId, 3> actors{};
        for (unsigned i = 0; i < names.size(); ++i) {
            const auto path = std::filesystem::path("Assets/AnimationValues") /
                              (std::string(names[i]) + ".animation.json");
            authored[i] = {read(root / path)};
            authored[i].validate();
            clips[i] = authored[i].asset();
            const auto& selected = catalog.records().at(clips[i]).metadata.at("forge.import");
            require(selected.at("platform") == "windows" && selected.at("backend") == "d3d12" &&
                        selected.at("output_version") == 2,
                    "Wrong selected value recipe/target");
            const auto artifact = root / ".forge/cache/derived" /
                                  selected.at("key").get<std::string>() / "animation.clip";
            require(asset_detail::read_bytes(artifact, transform_animation_byte_limit) ==
                        authored[i].cook(),
                    "Published value clip bytes differ from authored cook");
            for (const auto& row : source.at("entities"))
                if (row.at("components").contains("forge.transform_animator") &&
                    row.at("components").at("forge.transform_animator").at("clip").get<AssetId>() ==
                        clips[i])
                    actors[i] = row.at("id").get<EntityId>();
            require(bool(actors[i]), "Values Gallery actor missing");
        }
        std::vector<AssetId> keys;
        for (const auto& key : authored[0].clip().keys)
            keys.push_back(key.id);
        for (unsigned i = 1; i < 3; ++i) {
            auto expected = authored[0];
            expected.document["asset_id"] = clips[i];
            expected.transform_key_values(keys, i == 1 ? Double3{-.5, 1.3, 1} : Double3{1, 1, 1},
                                          i == 1 ? Double3{2, .3, 0} : Double3{},
                                          i == 2 ? Double3{2, .5, 1.5} : Double3{1, 1, 1});
            require(expected.document == authored[i].document,
                    "Gallery value transform source differs");
        }
        {
            World live(root, source);
            inspect(live, actors, 0);
            for (unsigned step = 1; step <= 32; ++step) {
                live.simulation.tick(.25f);
                inspect(live, actors, step * .25);
            }
        }
        const std::array roots{source.at("asset_id").get<AssetId>()};
        {
            auto lease = std::make_shared<ProjectLease>(root);
            prepare_runtime_content_catalog(*lease, roots, {});
        }
        const auto package = root.parent_path() / (root.filename().string() + "-export");
        (void)package_runtime_content(root, package, roots, {"windows", "d3d12"});
        const auto moved = std::filesystem::path(package.string() + "-relocated");
        std::filesystem::rename(package, moved);
        const auto shipped = open_runtime_content(moved, {"windows", "d3d12"});
        require(shipped.records().size() == 4, "Values Gallery selected unrelated dependencies");
        for (unsigned i = 0; i < names.size(); ++i)
            require(shipped.records().contains(clips[i]) &&
                        !std::filesystem::exists(moved / "Assets/AnimationValues" /
                                                 (std::string(names[i]) + ".animation.json")),
                    "Values selected cook missing or source leaked");
        const auto packaged_scene = read(moved / shipped.records().at(roots[0]).source);
        std::filesystem::rename(root, std::filesystem::path(root.string() + "-source-offline"));
        World game(moved, packaged_scene);
        inspect(game, actors, 0);
        for (unsigned step = 1; step <= 32; ++step) {
            game.simulation.tick(.25f);
            inspect(game, actors, step * .25);
        }
        std::cout << "Gallery value source/IDs, selected cooks, independent analytic motion, "
                     "render admission and relocated source-free export passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
