#include "runtime_package.hpp"
#include <cmath>
#include <forge/animation.hpp>
#include <forge/authoring.hpp>
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
void near(double a, double b) { require(std::abs(a - b) < .00005, "Gallery curve sample differs"); }
Json read(const std::filesystem::path& path) {
    Json j;
    std::ifstream(path) >> j;
    return j;
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
            require(std::chrono::steady_clock::now() < deadline, "Gallery curve admission timeout");
            std::this_thread::sleep_for(1ms);
        }
        simulation.reset_presentation();
    }
};
void inspect(World& world, const std::array<EntityId, 3>& actors, double time) {
    const double phase = time / 2., ease = phase * phase * (3 - 2 * phase);
    for (unsigned i = 0; i < 3; ++i) {
        const auto actor = world.scene.entity(actors[i].str());
        near(actor.get<LocalTranslation>().x, -3 + 6 * (i == 0 ? phase : ease));
        near(actor.get<LocalTranslation>().y, 1.5 + (i == 2 ? 4 * phase * (1 - phase) : 0));
        near(actor.get<LocalScale>().y, .4 + .4 * (i == 0 ? phase : ease));
        const auto expected = rotation_from_euler({0, 180 * phase, 0});
        const auto actual = actor.get<LocalRotation>();
        near(std::abs(actual.y), std::abs(expected.y));
        near(std::abs(actual.w), std::abs(expected.w));
    }
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Need Gallery and scratch root");
        const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        const auto source = read(root / "Scenes/13-transform-curves.scene.json");
        const auto catalog = AssetCatalog::open_project(root);
        const std::array<const char*, 3> files{"Platform.animation.json",
                                               "EasedPlatform.animation.json",
                                               "ArcPlatform.animation.json"};
        std::array<AssetId, 3> clips{};
        std::array<EntityId, 3> actors{};
        for (unsigned i = 0; i < 3; ++i) {
            TransformAnimationSource authored{read(root / "Assets" / files[i])};
            authored.validate();
            clips[i] = authored.asset();
            const auto& record = catalog.records().at(clips[i]);
            const auto& selected = record.metadata.at("forge.import");
            require(selected.at("platform") == "windows" && selected.at("backend") == "d3d12",
                    "Wrong Gallery curve cook target");
            require(selected.at("output_version") == unsigned(i == 0 ? 1 : 2),
                    "Legacy/new selected recipe differs");
            require(authored.document.at("version") == unsigned(i == 0 ? 1 : 2),
                    "Legacy/new source version differs");
            for (const auto& key : authored.clip().keys)
                require(key.curves.cubic == (i != 0), "Gallery vector mode differs");
            for (const auto& row : source.at("entities"))
                if (row.at("components").contains("forge.transform_animator") &&
                    row.at("components").at("forge.transform_animator").at("clip").get<AssetId>() ==
                        clips[i])
                    actors[i] = row.at("id").get<EntityId>();
            require(bool(actors[i]), "Gallery curve actor missing");
        }
        World live(root, source);
        live.simulation.tick(.5f);
        inspect(live, actors, .5);
        const auto checkpoint = live.animation->checkpoint();
        World recovered(root, live.scene.document());
        recovered.animation->restore(checkpoint);
        inspect(recovered, actors, .5);
        recovered.simulation.tick(.5f);
        inspect(recovered, actors, 1);
        // A second instance of the same clip has its own speed and playback clock.
        auto shared = source;
        Json clone;
        for (const auto& row : source.at("entities"))
            if (row.at("id").get<EntityId>() == actors[1])
                clone = row;
        const auto second = EntityId::generate();
        clone["id"] = second;
        clone["name"] = "Independent curve instance";
        clone["components"]["forge.transform_animator"]["playback_speed"] = .5;
        shared["entities"].push_back(clone);
        World instances(root, shared);
        instances.simulation.tick(.5f);
        inspect(instances, actors, .5);
        near(instances.scene.entity(second.str()).get<LocalTranslation>().x, -2.7421875);
        auto paused = instances.scene.entity(second.str()).get<TransformAnimator>();
        paused.playback_speed = 0;
        instances.scene.entity(second.str()).set<TransformAnimator>(paused);
        instances.simulation.tick(.5f);
        near(instances.scene.entity(second.str()).get<LocalTranslation>().x, -2.7421875);
        inspect(instances, actors, 1);
        auto finite = source;
        for (auto& row : finite["entities"])
            if (row["components"].contains("forge.transform_animator"))
                row["components"]["forge.transform_animator"]["loop"] = false;
        World ended(root, finite);
        for (unsigned i = 0; i < 17; ++i)
            ended.simulation.tick(.25f);
        for (const auto id : actors)
            near(ended.scene.entity(id.str()).get<LocalTranslation>().x, -3);
        live.simulation.tick(1.5f);
        for (unsigned i = 0; i < 3; ++i)
            near(live.scene.entity(actors[i].str()).get<LocalTranslation>().x, 3);
        live.simulation.tick(2.f);
        for (unsigned i = 0; i < 3; ++i)
            near(live.scene.entity(actors[i].str()).get<LocalTranslation>().x, -3);
        const std::array roots{source.at("asset_id").get<AssetId>()};
        auto lease = std::make_shared<ProjectLease>(root);
        prepare_runtime_content_catalog(*lease, roots, {});
        const auto package = root.parent_path() / (root.filename().string() + "-export");
        (void)package_runtime_content(root, package, roots, {"windows", "d3d12"});
        const auto moved = std::filesystem::path(package.string() + "-relocated");
        std::filesystem::rename(package, moved);
        const auto shipped = open_runtime_content(moved, {"windows", "d3d12"});
        for (unsigned i = 0; i < 3; ++i) {
            require(shipped.records().contains(clips[i]), "Curve dependency missing from export");
            require(!std::filesystem::exists(moved / "Assets" / files[i]),
                    "Curve source leaked into export");
        }
        std::filesystem::rename(root / "Assets", root / "source-offline");
        World game(moved, source);
        game.simulation.tick(.5f);
        inspect(game, actors, .5);
        require(shipped.records().size() == 4, "Curve scene selected unrelated dependencies");
        std::cout << "Legacy v1 and cubic v2 Gallery resource, runtime, independent clocks, "
                     "recovery and relocated source-free export passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
