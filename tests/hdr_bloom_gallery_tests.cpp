#include "model_render_resource.hpp"
#include "runtime_package.hpp"
#include <forge/animation.hpp>
#include <forge/material_source.hpp>
#include <forge/render_scene.hpp>
#include <forge/runtime.hpp>
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
void inspect(const std::filesystem::path& root, const Json& source,
             const std::array<AssetId, 2>& materials) {
    const auto catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
    EngineContext engine(WorldRole::Runtime);
    Scene scene(engine.world());
    scene.restore_snapshot(source);
    const auto admitted = scene.document();
    RuntimeSimulation simulation(engine.world(), scene);
    simulation.reset_presentation();
    for (unsigned tick = 0; tick < 3; ++tick) {
        const auto render = extract_render_scene(simulation.presentation(1));
        check(render.settings.bloom == SceneBloom{true, 1, .6f, 2} && render.cameras.size() == 1 &&
                  render.meshes.size() == 4 && render.lights.size() == 1 &&
                  render.diagnostics.empty(),
              "Bloom Gallery runtime render admission differs");
        std::set<AssetId> observed;
        for (const auto& actor : render.meshes)
            for (const auto& slot : actor.renderer.materials)
                if (std::find(materials.begin(), materials.end(), slot.material.id) !=
                    materials.end())
                    observed.insert(slot.material.id);
        check(observed == std::set<AssetId>(materials.begin(), materials.end()),
              "Bloom Gallery lost typed emissive assignments");
        simulation.tick(1.f / 60);
    }
    check(scene.document() == admitted, "Bloom runtime rewrote authored scene");
    const std::array<std::array<float, 3>, 2> colors{{{.6f, .12f, .03f}, {.02f, .3f, .6f}}};
    for (unsigned i = 0; i < materials.size(); ++i) {
        ResourcePool<MaterialAsset> pool;
        const auto ticket =
            asset_detail::request_model_pbr_material(pool, root, catalog, {materials[i]});
        check(pool.wait(ticket, std::chrono::seconds(5)) && bool(pool.acquire(ticket)),
              "Bloom Gallery cooked material admission failed");
        const auto material = pool.acquire(ticket);
        check(material->values.parameters.at("emissiveStrength").value[0] == 12 &&
                  material->values.parameters.at("emissiveFactor").type ==
                      MaterialParameterType::LinearColor3 &&
                  material->values.parameters.at("emissiveFactor").value[3] == 0,
              "Bloom Gallery did not load exact emissive parameters");
        for (unsigned c = 0; c < 3; ++c)
            check(std::abs(material->values.parameters.at("emissiveFactor").value[c] -
                           colors[i][c]) < 1e-7,
                  "Bloom Gallery emissive color differs");
    }
}
void inspect_color(const std::filesystem::path& root) {
    const auto catalog = AssetCatalog::open_project(root);
    const auto id = AssetId::parse("7456ed81-3d3b-45a6-8c7d-6b50073ad01a");
    const auto source = read(root / catalog.records().at(id).source);
    EngineContext engine(WorldRole::Runtime, false);
    Scene scene(engine.world());
    scene.restore_snapshot(source);
    RuntimeSimulation simulation(engine.world(), scene);
    for (unsigned i = 0; i < 10; ++i)
        simulation.tick(1.f / 60);
    const auto frame = extract_render_scene(simulation.presentation(1));
    const auto cameras = prepare_game_cameras(frame, 384, 192);
    check(scene.document() == source && frame.meshes.size() == 8 && cameras.cameras.size() == 1 &&
              frame.diagnostics.empty() &&
              frame.settings.color_adjustments ==
                  SceneColorAdjustments{true, {1.1f, .85f, .7f}, .8f, 1.15f},
          "Gallery69 runtime Scene, geometry, camera or authored grading changed");
}
AssetId inspect_distance_fog(const std::filesystem::path& root) {
    const auto catalog = AssetCatalog::open_project(root);
    const auto id = AssetId::parse("da66d15d-42ed-4342-94ce-19d67de0e105");
    const auto source = read(root / catalog.records().at(id).source);
    EngineContext engine(WorldRole::Runtime, false);
    Scene scene(engine.world());
    scene.restore_snapshot(source);
    RuntimeSimulation simulation(engine.world(), scene);
    simulation.reset_presentation();
    const auto frame = extract_render_scene(simulation.presentation(1));
    check(frame.settings.distance_fog == SceneDistanceFog{true, {.25f, .35f, .5f}, 12, 34} &&
              frame.meshes.size() == 13 && frame.cameras.size() == 1 && frame.lights.size() == 1 &&
              frame.diagnostics.empty() && scene.document() == source,
          "Distance fog Gallery runtime settings, geometry or Scene changed");
    return id;
}
AssetId inspect_exposure(const std::filesystem::path& root) {
    const auto catalog = AssetCatalog::open_project(root);
    const auto found =
        catalog.records().find(AssetId::parse("43420ace-33d5-4768-ae3f-771fd7ec384a"));
    check(found != catalog.records().end(), "Gallery96 Scene is missing");
    const auto source = read(root / found->second.source);
    EngineContext engine(WorldRole::Runtime, false, {animation_module(root)});
    Scene scene(engine.world());
    scene.restore_snapshot(source);
    const auto authored = scene.document();
    const auto animation = animation_runtime(engine.world());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!animation->prepare_initial_pose()) {
        check(std::chrono::steady_clock::now() < deadline, "Gallery96 light clip timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    RuntimeSimulation simulation(engine.world(), scene);
    simulation.reset_presentation();
    for (unsigned step = 0; step < 32; ++step) {
        const auto frame = extract_render_scene(simulation.presentation(1));
        const double expected = step >= 8 && step <= 16 ? 24 : .08;
        check(frame.settings.auto_exposure ==
                      SceneAutoExposure{true, -8, 8, .02f, .98f, .18f, 1, 3} &&
                  frame.cameras.size() == 1 && frame.lights.size() == 1 &&
                  frame.meshes.size() == 8 &&
                  std::abs(frame.lights[0].light.intensity - expected) < 1e-5 &&
                  frame.diagnostics.empty(),
              "Gallery96 lost authored exposure, held light values or rendering consumers");
        simulation.tick(.25f);
    }
    check(read(root / found->second.source) == source, "Gallery96 runtime rewrote the saved Scene");
    // Animation changes its runtime world; the immutable authored source remains
    // the publication owner, including the initially dim light value.
    check(authored.at("rendering") == source.at("rendering"),
          "Gallery96 admission changed authored rendering settings");
    return found->first;
}
AssetId inspect_temporal(const std::filesystem::path& root) {
    const auto catalog = AssetCatalog::open_project(root);
    const auto id = AssetId::parse("4df8a08b-a5e1-407b-8321-5f80efa720e2");
    const auto source = read(root / catalog.records().at(id).source);
    EngineContext engine(WorldRole::Runtime, false, {animation_module(root)});
    Scene scene(engine.world());
    scene.restore_snapshot(source);
    const auto animation = animation_runtime(engine.world());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!animation->prepare_initial_pose()) {
        check(std::chrono::steady_clock::now() < deadline, "Temporal Gallery clip timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    RuntimeSimulation simulation(engine.world(), scene);
    simulation.reset_presentation();
    const auto first = extract_render_scene(simulation.presentation(1));
    for (unsigned i = 0; i < 90; ++i)
        simulation.tick(1.f / 60);
    const auto second = extract_render_scene(simulation.presentation(1));
    check(first.settings.temporal_anti_aliasing == SceneTemporalAntiAliasing{true, .85f} &&
              first.meshes.size() == 22 && first.cameras.size() == 1 && first.lights.size() == 1 &&
              first.diagnostics.empty() && second.diagnostics.empty(),
          "Temporal Gallery lost settings or shared geometry admission");
    bool moved = false;
    for (const auto& a : first.meshes)
        for (const auto& b : second.meshes)
            moved |= a.entity == b.entity && a.world != b.world;
    check(moved && read(root / catalog.records().at(id).source) == source,
          "Temporal Gallery did not animate independently of its saved Scene");
    return id;
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need Feature Gallery and scratch root");
        const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        const auto path = std::filesystem::path("Scenes/48-hdr-bloom.scene.json");
        const auto source = read(root / path);
        const std::array paths{"Assets/Bloom/WarmGlow.material.json",
                               "Assets/Bloom/CoolGlow.material.json"};
        std::array<AssetId, 2> materials;
        const auto catalog = AssetCatalog::open_project(root);
        for (unsigned i = 0; i < materials.size(); ++i) {
            MaterialSource material{read(root / paths[i])};
            material.validate();
            materials[i] = material.asset();
            const auto& selected = catalog.records().at(materials[i]).metadata.at("forge.import");
            check(selected.at("platform") == "windows" && selected.at("backend") == "d3d12",
                  "Bloom Gallery selected wrong material target");
        }
        inspect(root, source, materials);
        inspect_color(root);
        const auto exposure_scene = inspect_exposure(root);
        const auto fog_scene = inspect_distance_fog(root);
        const auto temporal_scene = inspect_temporal(root);
        const std::array roots{source.at("asset_id").get<AssetId>(),
                               AssetId::parse("7456ed81-3d3b-45a6-8c7d-6b50073ad01a"),
                               exposure_scene, fog_scene, temporal_scene};
        {
            auto lease = std::make_shared<ProjectLease>(root);
            prepare_runtime_content_catalog(*lease, roots, {});
        }
        const auto package = root.parent_path() / (root.filename().string() + "-export");
        (void)package_runtime_content(root, package, roots, {"windows", "d3d12"});
        const auto moved = std::filesystem::path(package.string() + "-relocated");
        std::filesystem::rename(package, moved);
        const auto shipped = open_runtime_content(moved, {"windows", "d3d12"});
        std::set<AssetId> expected(roots.begin(), roots.end());
        expected.insert(materials.begin(), materials.end());
        expected.insert(AssetId::parse("4ab91f02-df69-47c5-a626-3b6c7504e954"));
        expected.insert(AssetId::parse("68d22705-0b64-4773-9661-8c28465e0d98"));
        expected.insert(AssetId::parse("399c3c5e-f4e9-4ccb-94b3-bc0e518c1ece"));
        std::set<AssetId> actual;
        for (const auto& [id, record] : shipped.records()) {
            (void)record;
            actual.insert(id);
        }
        check(actual == expected && expected.size() == 10,
              "Bloom/color/exposure/fog export changed its exact shared asset closure");
        check(!std::filesystem::exists(moved / "Assets/Exposure/Daylight.animation.json"),
              "Exposure clip source leaked into the runtime package");
        for (const auto path : paths)
            check(!std::filesystem::exists(moved / path),
                  "Bloom material source leaked into export");
        const auto exported_source = read(moved / shipped.records().at(roots[0]).source);
        std::filesystem::rename(root, std::filesystem::path(root.string() + "-source-offline"));
        inspect(moved, exported_source, materials);
        inspect_color(moved);
        check(inspect_exposure(moved) == exposure_scene,
              "Gallery96 relocation changed its stable Scene identity");
        check(inspect_distance_fog(moved) == fog_scene,
              "Gallery98 relocation changed fog settings or stable Scene identity");
        check(inspect_temporal(moved) == temporal_scene &&
                  !std::filesystem::exists(moved / "Assets/Platform.animation.json"),
              "Temporal Gallery relocation changed identity or leaked its animation source");
        std::cout
            << "Gallery102 temporal settings, animated geometry and source-free relocation PASS\n";
        std::cout << "Gallery98 distance fog settings and source-free relocation PASS\n";
        std::cout
            << "Gallery48 runtime/cooked material/scene and relocated source-free export PASS\n";
        std::cout << "Gallery69 color settings and relocated source-free export PASS: "
                  << moved.string() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
