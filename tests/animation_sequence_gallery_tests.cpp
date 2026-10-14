#include "animation_playback_access.hpp"
#include "runtime_package.hpp"
#include <forge/animation.hpp>
#include <forge/authoring.hpp>
#include <forge/render_scene.hpp>
#include <forge/runtime.hpp>
#include <fstream>
#include <iostream>
#include <numbers>
#include <thread>
using namespace forge;
namespace {
void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
void near(double actual, double expected) {
    if (std::abs(actual - expected) > .00001)
        throw std::runtime_error("Sequence Gallery oracle: " + std::to_string(actual) + " vs " +
                                 std::to_string(expected));
}
Json read(const std::filesystem::path& path) {
    Json value;
    std::ifstream(path) >> value;
    return value;
}
double ease(double time, double offset, double duration) {
    const auto u = std::clamp((time - offset) / duration, 0., 1.);
    return u * u * (3 - 2 * u);
}
struct World {
    EngineContext engine;
    Scene scene;
    RuntimeSimulation simulation;
    std::shared_ptr<AnimationRuntime> animation;
    std::shared_ptr<AudioRuntime> audio;
    World(const std::filesystem::path& root, const Json& source)
        : engine(WorldRole::Runtime, false,
                 {animation_module(root), audio_module({root, AudioOutput::Offline, true})}),
          scene(engine.world()), simulation(engine.world(), scene),
          animation(animation_runtime(engine.world())),
          audio(std::static_pointer_cast<AudioRuntime>(engine.services().audio())) {
        scene.restore_snapshot(source);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (!animation->prepare_initial_pose()) {
            check(std::chrono::steady_clock::now() < deadline,
                  "Sequence Gallery admission stalled");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        simulation.reset_presentation();
        simulation.sync_audio();
        check(audio->wait_for_sources(std::chrono::seconds(5)), "Sequence sound failed to load");
        simulation.audio_paused(false);
    }
};
struct Actors {
    EntityRef controller;
    EntityId door, camera, light;
    explicit Actors(const Json& source) {
        for (const auto& row : source.at("entities")) {
            const auto& components = row.at("components");
            const auto id = row.at("id").get<EntityId>();
            if (components.contains("forge.animation_sequence"))
                controller = {source.at("asset_id").get<AssetId>(), id};
            if (!components.contains("forge.transform_animator"))
                continue;
            if (components.contains("forge.camera"))
                camera = id;
            else if (components.contains("forge.light"))
                light = id;
            else
                door = id;
        }
        check(bool(controller.entity) && bool(door) && bool(camera) && bool(light),
              "Gallery sequence participants are missing");
    }
};
void inspect(World& world, const Actors& actors, double time, bool interpolated) {
    const auto door = world.scene.entity(actors.door.str());
    const auto camera = world.scene.entity(actors.camera.str());
    const auto light = world.scene.entity(actors.light.str());
    near(world.animation->playback(actors.controller)->time, time);
    near(door.get<LocalTranslation>().x, -2 + 4 * ease(time, .5, 2));
    near(door.get<LocalTranslation>().y, 1.5);
    near(door.get<LocalScale>().y, 3); // Translation-only clip preserves authored size.
    near(camera.get<LocalTranslation>().z, -10 + 2 * ease(time, 0, 3.5));
    near(camera.get<Camera>().vertical_fov,
         (65 - 15 * ease(time, 0, 3.5)) * std::numbers::pi / 180);
    const auto blend = ease(time, 1, 2);
    near(light.get<Light>().intensity, .8 + 2.2 * blend);
    near(light.get<Light>().color_r, .2 + .8 * blend);
    near(light.get<AudioSource>().gain, .05 + .25 * blend);
    const auto capture = world.simulation.presentation(interpolated ? .5 : 1);
    const auto render = extract_render_scene(capture);
    check(render.diagnostics.empty() && render.cameras.size() == 1 && render.lights.size() == 1 &&
              render.meshes.size() == 5,
          "Gallery sequence lost actual camera/light/mesh consumers");
    const double sampled = interpolated && time >= .25 ? time - .125 : time;
    near(render.cameras.front().camera.vertical_fov,
         (65 - 15 * ease(sampled, 0, 3.5)) * std::numbers::pi / 180);
    near(render.lights.front().light.intensity, .8 + 2.2 * ease(sampled, 1, 2));
    check(world.simulation.presentation(interpolated ? .5 : 1) == capture,
          "Copied presentation advanced sequence time");
}
void exercise(const std::filesystem::path& root, const Json& source) {
    Actors actors(source);
    World live(root, source);
    inspect(live, actors, 0, false);
    for (unsigned tick = 1; tick <= 25; ++tick) {
        live.simulation.tick(.25f);
        inspect(live, actors, std::fmod(tick * .25, 6.), true);
    }
    AnimationPlaybackAccess::control(*live.animation, actors.controller,
                                     {{"operation", "seek"}, {"position_seconds", 2.5}});
    AnimationPlaybackAccess::control(*live.animation, actors.controller, {{"operation", "pause"}});
    live.simulation.reset_presentation();
    const auto checkpoint = live.animation->checkpoint();
    World recovered(root, source);
    recovered.animation->restore(checkpoint);
    recovered.simulation.reset_presentation();
    inspect(recovered, actors, 2.5, false);
    live.simulation.tick(.25f);
    inspect(live, actors, 2.5, false);
    live.simulation.sync_audio();
    std::vector<float> pcm(48000 * 2);
    live.audio->read_offline(pcm);
    double energy = 0;
    for (const auto value : pcm) {
        check(std::isfinite(value), "Sequence audio produced nonfinite PCM");
        energy += double(value) * value;
    }
    check(energy > 1, "Animated sequence gain did not reach the offline audio consumer");
    const auto before = live.scene.entity(actors.door.str()).get<LocalTranslation>();
    live.scene.entity(actors.light.str()).remove<Light>();
    AnimationPlaybackAccess::control(*live.animation, actors.controller, {{"operation", "play"}});
    live.simulation.tick(.25f);
    check(live.scene.entity(actors.door.str()).get<LocalTranslation>() == before,
          "Invalid property target allowed partial sequence writes");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need Gallery and sequence scratch root");
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(scratch);
        const auto root = scratch / "source";
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        const auto source = read(root / "Scenes/62-coordinated-animation.scene.json");
        const auto catalog = AssetCatalog::open_project(root);
        std::set<AssetId> clips;
        for (const auto& row : source.at("entities"))
            if (row.at("components").contains("forge.transform_animator")) {
                const auto id =
                    row.at("components").at("forge.transform_animator").at("clip").get<AssetId>();
                clips.insert(id);
                const auto& selected = catalog.records().at(id).metadata.at("forge.import");
                check(selected.at("platform") == "windows" && selected.at("backend") == "d3d12",
                      "Sequence Gallery selected the wrong clip target");
            }
        check(clips.size() == 3, "Sequence Gallery did not reuse three independent clips");
        exercise(root, source);
        const std::array roots{source.at("asset_id").get<AssetId>()};
        {
            ProjectLease lease(root);
            prepare_runtime_content_catalog(lease, roots, {});
        }
        const auto exported = scratch / "export";
        package_runtime_content(root, exported, roots, {"windows", "d3d12"});
        const auto moved = scratch / "relocated";
        std::filesystem::rename(exported, moved);
        const auto shipped = open_runtime_content(moved, {"windows", "d3d12"});
        check(shipped.records().size() == 5, "Sequence export has an incorrect content closure");
        for (const auto id : clips)
            check(shipped.records().contains(id) &&
                      !std::filesystem::exists(moved / catalog.records().at(id).source),
                  "Sequence export lost a selected cook or shipped editable clip source");
        std::filesystem::rename(root, scratch / "source-offline");
        exercise(moved, read(moved / shipped.records().at(roots[0]).source));
        std::cout << "Gallery62 coordinated transform/camera/light/audio, holds/loop/controls, "
                     "recovery/candidates and relocated source-free Windows-target data PASS\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
