#include "animation_playback_access.hpp"
#include "runtime_package.hpp"
#include <forge/animation.hpp>
#include <forge/game_content.hpp>
#include <forge/render_scene.hpp>
#include <forge/runtime.hpp>
#include <forge/runtime_resources.hpp>
#include <fstream>
#include <iostream>
#include <thread>
using namespace forge;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
Json read(const std::filesystem::path& path) {
    Json result;
    std::ifstream(path) >> result;
    return result;
}
void near(double value, double expected) {
    if (std::abs(value - expected) > 1e-5)
        throw std::runtime_error("Gallery67 independent oracle: " + std::to_string(value) + " vs " +
                                 std::to_string(expected));
}
double parameter(const MeshRenderer& renderer, const char* key, unsigned lane) {
    const auto at =
        std::find_if(renderer.material_parameters.begin(), renderer.material_parameters.end(),
                     [&](const auto& row) { return row.key == key; });
    check(at != renderer.material_parameters.end(), "Gallery67 runtime omitted a material value");
    return at->parameter.value[lane];
}
void exercise(const std::filesystem::path& root, AssetId scene_id) {
    const auto source = load_game_scene(root, {scene_id});
    const auto source_path = root / AssetCatalog::open_project(root).records().at(scene_id).source;
    const auto saved = read(source_path);
    EngineContext engine(WorldRole::Runtime, false,
                         {runtime_resources_module(root), animation_module(root)});
    Scene scene(engine.world());
    scene.restore_snapshot(source);
    RuntimeSimulation simulation(engine.world(), scene);
    const auto animation = animation_runtime(engine.world());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    for (;;) {
        engine.services().resources()->synchronize();
        if (animation->prepare_initial_pose())
            break;
        check(std::chrono::steady_clock::now() < deadline, "Gallery67 animation admission stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    simulation.reset_presentation();
    std::map<std::string, flecs::entity> actors;
    for (const auto& row : source.at("entities"))
        actors.emplace(row.at("name").get<std::string>(),
                       scene.entity(row.at("id").get<std::string>()));
    auto value = [&](const char* name, double fraction) {
        const auto& renderer = actors.at(name).get<MeshRenderer>();
        near(parameter(renderer, "baseColorFactor", 0), .9 + (.02 - .9) * fraction);
        near(parameter(renderer, "baseColorFactor", 2), .02 + (.9 - .02) * fraction);
        near(parameter(renderer, "roughnessFactor", 0), .15 + .7 * fraction);
    };
    for (unsigned tick = 0; tick <= 36; ++tick) {
        const auto elapsed = std::fmod(double(tick) * .125, 4.);
        const auto linear = elapsed < 2 ? elapsed / 2 : (4 - elapsed) / 2;
        const auto smooth = linear * linear * (3 - 2 * linear);
        const auto held = elapsed < 2 ? 0. : 1.;
        value("Linear color and roughness", linear);
        value("Smooth color and roughness - Prefab", smooth);
        value("Held color and roughness", held);
        check(actors.at("Shared material - unchanged")
                  .get<MeshRenderer>()
                  .material_parameters.empty(),
              "Animating one cube changed the shared material/default cube");
        const auto render = extract_render_scene(simulation.presentation(.5));
        check(render.meshes.size() == 5 && render.cameras.size() == 1 &&
                  render.lights.size() == 1 && render.diagnostics.empty(),
              "Gallery67 render extraction rejected the scene");
        if (tick < 36)
            simulation.tick(.125f);
    }
    // Runtime writes stay out of source/Prefab intent. Recovery uses the actual
    // shared resource/animation provider boundary and restores the saved clocks.
    const auto checkpoint = animation->checkpoint();
    simulation.tick(.125f);
    animation->restore(checkpoint);
    value("Linear color and roughness", .25);
    value("Smooth color and roughness - Prefab", .15625);
    check(read(source_path) == saved,
          "Gallery67 runtime/recovery rewrote authored Scene/Prefab intent");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need Gallery and scratch root");
        const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto source = root / "source", output = root / "export";
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], source, std::filesystem::copy_options::recursive);
        const auto scene_id = read(source / "Scenes/67-animated-material-values.scene.json")
                                  .at("asset_id")
                                  .get<AssetId>();
        const auto catalog = AssetCatalog::open_project(source);
        for (const auto* name : {"Linear", "Smooth", "Held"}) {
            const auto path =
                "Assets/ObjectValues/" + std::string(name) + "Appearance.animation.json";
            const auto id = read(source / path).at("asset_id").get<AssetId>();
            const auto& selected = catalog.records().at(id).metadata.at("forge.import");
            check(selected.at("platform") == "windows" && selected.at("backend") == "d3d12" &&
                      selected.at("output_version") == 5,
                  "Gallery67 selected wrong clip target/version");
        }
        exercise(source, scene_id);
        const std::array roots{scene_id};
        const RuntimePackageTarget target{"windows", "d3d12"};
        (void)package_runtime_content(source, output, roots, target);
        std::filesystem::rename(source, root / "offline-source");
        const auto moved = root / "relocated-game";
        std::filesystem::rename(output, moved);
        const auto shipped = open_runtime_content(moved, target);
        check(shipped.records().size() == 6, "Gallery67 export included unrelated content");
        for (const auto& [id, record] : shipped.records()) {
            (void)id;
            if (record.type == TransformAnimationAsset::type || record.type == MaterialAsset::type)
                check(std::filesystem::exists(moved / record.source),
                      "Gallery67 selected cooked artifact is missing");
        }
        for (const auto* name : {"Linear", "Smooth", "Held"})
            check(!std::filesystem::exists(moved / ("Assets/ObjectValues/" + std::string(name) +
                                                    "Appearance.animation.json")),
                  "Gallery67 authored animation source leaked into export");
        check(!std::filesystem::exists(moved / "Assets/ObjectValues/SharedSurface.material.json"),
              "Gallery67 authored material source leaked into export");
        check(!std::filesystem::exists(moved / "CodeExamples"),
              "Gallery67 export depends on source examples");
        exercise(moved, scene_id);
        std::cout << "Gallery67 actual fixed/presentation/recovery, Prefab intent and moved "
                     "source-offline export PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
