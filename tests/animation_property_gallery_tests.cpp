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
Json read(const std::filesystem::path& path) {
    Json result;
    std::ifstream(path) >> result;
    return result;
}
void near(double a, double b) {
    check(std::abs(a - b) < 1e-5, "Gallery49 analytic property value differs");
}
void exercise(const std::filesystem::path& root, const Json& source) {
    const auto source_path =
        root /
        AssetCatalog::open_project(root).records().at(source.at("asset_id").get<AssetId>()).source;
    const auto saved_source = read(source_path);
    EngineContext engine(WorldRole::Runtime, false, {animation_module(root)});
    Scene scene(engine.world());
    scene.restore_snapshot(source);
    RuntimeSimulation simulation(engine.world(), scene);
    auto animation = animation_runtime(engine.world());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!animation->prepare_initial_pose()) {
        check(std::chrono::steady_clock::now() < deadline, "Gallery49 clip admission stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    simulation.reset_presentation();
    EntityId camera, light;
    for (const auto& row : source.at("entities")) {
        if (row.at("components").contains("forge.camera"))
            camera = row.at("id").get<EntityId>();
        if (row.at("components").contains("forge.light"))
            light = row.at("id").get<EntityId>();
    }
    check(bool(camera) && bool(light), "Gallery49 camera/light target missing");
    double elapsed = 0;
    const auto fraction = [](double time) {
        time = std::fmod(time, 4.);
        return time <= 2 ? time / 2 : (4 - time) / 2;
    };
    for (unsigned step = 0; step < 20; ++step) {
        const auto f = fraction(elapsed);
        near(scene.entity(camera.str()).get<Camera>().vertical_fov,
             (40 + 40 * f) * std::numbers::pi / 180);
        near(scene.entity(light.str()).get<Light>().intensity, 1 + 3 * f);
        near(scene.entity(light.str()).get<Light>().color_r, .2 + .8 * f);
        const auto render = extract_render_scene(simulation.presentation(.5));
        check(render.cameras.size() == 1 && render.lights.size() == 1 &&
                  render.meshes.size() == 4 && render.diagnostics.empty(),
              "Gallery49 rendering consumers differ");
        const auto sampled = fraction(std::max(0., elapsed - .125));
        near(render.cameras[0].camera.vertical_fov, (40 + 40 * sampled) * std::numbers::pi / 180);
        near(render.lights[0].light.intensity, 1 + 3 * sampled);
        simulation.tick(.25f);
        elapsed += .25;
    }
    check(read(source_path) == saved_source, "Gallery49 animation rewrote saved scene");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need Gallery and scratch root");
        const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        const auto path = std::filesystem::path("Scenes/49-animated-camera-and-light.scene.json");
        const auto source = read(root / path);
        const auto catalog = AssetCatalog::open_project(root);
        for (const auto name : {"CameraZoom", "LightPulse"}) {
            const auto relative = std::filesystem::path("Assets/AnimatedProperties") /
                                  (std::string(name) + ".animation.json");
            const auto authored = read(root / relative);
            const auto& selected = catalog.records()
                                       .at(authored.at("asset_id").get<AssetId>())
                                       .metadata.at("forge.import");
            check(selected.at("platform") == "windows" && selected.at("backend") == "d3d12" &&
                      selected.at("output_version") == 3,
                  "Gallery49 selected wrong clip target/version");
        }
        exercise(root, source);
        const std::array roots{source.at("asset_id").get<AssetId>()};
        {
            auto lease = std::make_shared<ProjectLease>(root);
            prepare_runtime_content_catalog(*lease, roots, {});
        }
        const auto exported = std::filesystem::path(root.string() + "-export");
        (void)package_runtime_content(root, exported, roots, {"windows", "d3d12"});
        const auto moved = std::filesystem::path(exported.string() + "-relocated");
        std::filesystem::rename(exported, moved);
        const auto shipped = open_runtime_content(moved, {"windows", "d3d12"});
        check(shipped.records().size() == 3, "Gallery49 export included unrelated content");
        check(!std::filesystem::exists(moved /
                                       "Assets/AnimatedProperties/CameraZoom.animation.json") &&
                  !std::filesystem::exists(moved /
                                           "Assets/AnimatedProperties/LightPulse.animation.json"),
              "Property source leaked into game export");
        std::filesystem::rename(root, std::filesystem::path(root.string() + "-source-offline"));
        exercise(moved, read(moved / shipped.records().at(roots[0]).source));
        std::cout << "Gallery49 actual fixed/presentation/render consumers and relocated "
                     "source-free export PASS\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
