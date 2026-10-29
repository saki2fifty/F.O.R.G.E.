#include "model_render_resource.hpp"
#include "runtime_package.hpp"
#include <algorithm>
#include <forge/animation.hpp>
#include <forge/engine_assets.hpp>
#include <forge/material_source.hpp>
#include <forge/render_scene.hpp>
#include <forge/runtime.hpp>
#include <fstream>
#include <iostream>
#include <map>
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
    // Independent additive-schema oracle. Existing files are never rewritten.
    if (result.contains("entities"))
        for (auto& row : result.at("entities")) {
            auto& components = row.at("components");
            if (auto light = components.find("forge.light"); light != components.end())
                for (const auto* field : {"shadow_source_radius", "shadow_angular_radius"})
                    if (!light->contains(field))
                        (*light)[field] = 0.;
        }
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
AssetId inspect_temporal(const std::filesystem::path& root, bool motion = false) {
    const auto catalog = AssetCatalog::open_project(root);
    const auto id = AssetId::parse(motion ? "38a4d328-ee99-4b5e-88b0-907902377829"
                                          : "4df8a08b-a5e1-407b-8321-5f80efa720e2");
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
    for (unsigned i = 0; i < (motion ? 15u : 90u); ++i)
        simulation.tick(1.f / 60);
    const auto second = extract_render_scene(simulation.presentation(1));
    check((motion
               ? first.settings.motion_blur == SceneMotionBlur{true, 1, 24}
               : first.settings.temporal_anti_aliasing == SceneTemporalAntiAliasing{true, .85f}) &&
              first.meshes.size() == (motion ? 256 : 22) && first.cameras.size() == 1 &&
              first.lights.size() == 1 && first.diagnostics.empty() && second.diagnostics.empty(),
          "Temporal Gallery lost settings or shared geometry admission");
    unsigned moved = 0;
    for (const auto& a : first.meshes)
        for (const auto& b : second.meshes)
            moved += a.entity == b.entity && a.world != b.world;
    check((motion ? moved == 128 : moved != 0) &&
              read(root / catalog.records().at(id).source) == source,
          "Temporal Gallery did not animate independently of its saved Scene");
    return id;
}
AssetId inspect_focus(const std::filesystem::path& root) {
    const auto catalog = AssetCatalog::open_project(root);
    const auto id = AssetId::parse("4af485f2-660b-45e6-a9d5-a33a80ba8983");
    const auto source = read(root / catalog.records().at(id).source);
    EngineContext engine(WorldRole::Runtime, false);
    Scene scene(engine.world());
    scene.restore_snapshot(source);
    RuntimeSimulation simulation(engine.world(), scene);
    simulation.reset_presentation();
    const auto frame = extract_render_scene(simulation.presentation(1));
    std::map<double, unsigned> planes;
    for (const auto& mesh : frame.meshes)
        ++planes[mesh.world.m[11]];
    check(frame.settings.depth_of_field == SceneDepthOfField{true, 9.95f, 2, 10},
          "Focus Gallery settings differ from saved focus values");
    check(frame.meshes.size() == 192,
          ("Focus Gallery Mesh count: " + std::to_string(frame.meshes.size())).c_str());
    check(frame.cameras.size() == 1 && frame.lights.size() == 1 &&
              frame.cameras[0].camera.projection == 1,
          "Focus Gallery lost its orthographic camera or light");
    check(planes == std::map<double, unsigned>{{0, 64}, {6, 64}, {12, 64}},
          ("Focus Gallery depth planes differ: " + Json(planes).dump()).c_str());
    if (!frame.diagnostics.empty())
        throw std::runtime_error("Focus Gallery: " + frame.diagnostics.front().text);
    check(read(root / catalog.records().at(id).source) == source,
          "Focus Gallery admission rewrote its saved Scene");
    return id;
}
AssetId inspect_occlusion(const std::filesystem::path& root) {
    const auto catalog = AssetCatalog::open_project(root);
    const auto id = AssetId::parse("d4b6e7a5-5c2a-4ee0-852d-cb53ffc7319a");
    const auto source = read(root / catalog.records().at(id).source);
    EngineContext engine(WorldRole::Runtime, false);
    Scene scene(engine.world());
    scene.restore_snapshot(source);
    RuntimeSimulation simulation(engine.world(), scene);
    simulation.reset_presentation();
    const auto frame = extract_render_scene(simulation.presentation(1));
    check(frame.settings.ambient_occlusion == SceneAmbientOcclusion{true, 1.1f, 1, .01f} &&
              frame.settings.environment.texture ==
                  engine_texture(EngineTexture::White, TextureDimension::Cube) &&
              frame.settings.environment.intensity == .7f && !frame.settings.shadows.enabled &&
              frame.meshes.size() == 5 && frame.cameras.size() == 1 && frame.lights.size() == 1 &&
              frame.diagnostics.empty(),
          "Ambient Gallery lost its lighting, five Mesh surfaces or rendering values");
    std::set<std::array<float, 4>> colors;
    for (const auto& mesh : frame.meshes) {
        check(!mesh.legacy_tint && mesh.renderer.material_parameters.size() == 1,
              "Ambient Gallery used unlit blockout appearance instead of lit object values");
        const auto& value = mesh.renderer.material_parameters.front();
        check(value.slot == "surface" && value.key == "baseColorFactor" &&
                  value.parameter.type == MaterialParameterType::LinearColor4,
              "Ambient Gallery lost a typed lit surface color");
        colors.insert(value.parameter.value);
    }
    check(colors == std::set<std::array<float, 4>>{{.48f, .65f, .78f, 1},
                                                   {.9f, .45f, .12f, 1},
                                                   {.25f, .36f, .48f, 1}},
          "Ambient Gallery object colors differ from its saved comparison");
    check(read(root / catalog.records().at(id).source) == source,
          "Ambient Gallery admission rewrote the saved Scene");
    return id;
}
AssetId inspect_reflections(const std::filesystem::path& root, bool rough = false) {
    const auto catalog = AssetCatalog::open_project(root);
    const auto id = AssetId::parse(rough ? "03d7f430-77ba-4c78-b75a-ff3a638ea3d9"
                                         : "120a1fb2-f8af-4fad-a644-eb598988ffab");
    const auto source = read(root / catalog.records().at(id).source);
    EngineContext engine(WorldRole::Runtime, false);
    Scene scene(engine.world());
    scene.restore_snapshot(source);
    RuntimeSimulation simulation(engine.world(), scene);
    simulation.reset_presentation();
    const auto frame = extract_render_scene(simulation.presentation(1));
    check(frame.settings.screen_space_reflections ==
                  SceneScreenSpaceReflections{true, 25, .15f, 1, rough ? .9f : .5f, rough} &&
              frame.settings.environment.texture ==
                  engine_texture(EngineTexture::White, TextureDimension::Cube) &&
              frame.settings.environment.intensity == .18f && !frame.settings.environment.sky &&
              !frame.settings.shadows.enabled && frame.meshes.size() == (rough ? 13 : 4) &&
              frame.cameras.size() == 1 && frame.lights.size() == 1 && frame.diagnostics.empty(),
          "Reflection Gallery lost material geometry, lighting or saved settings");
    unsigned receivers = 0;
    for (const auto& mesh : frame.meshes) {
        check(!mesh.legacy_tint && mesh.renderer.material_parameters.size() == 3,
              "Reflection Gallery lost explicit lit object material values");
        std::map<std::string, MaterialParameter> values;
        for (const auto& value : mesh.renderer.material_parameters) {
            check(value.slot == "surface", "Reflection Gallery material slot changed");
            values.emplace(value.key, value.parameter);
        }
        check(values.at("baseColorFactor").type == MaterialParameterType::LinearColor4 &&
                  values.at("metallicFactor").type == MaterialParameterType::Scalar &&
                  values.at("roughnessFactor").type == MaterialParameterType::Scalar,
              "Reflection Gallery lost typed base material parameters");
        receivers += values.at("metallicFactor").value[0] == 1 &&
                     values.at("roughnessFactor").value[0] == (rough ? .38f : .07f);
    }
    check(receivers == 1 && read(root / catalog.records().at(id).source) == source,
          "Reflection Gallery lost its single smooth metal receiver or rewrote Scene");
    return id;
}
struct ProbeGallery {
    AssetId scene;
    std::array<AssetId, 2> maps;
};
ProbeGallery inspect_local_probes(const std::filesystem::path& root) {
    const auto catalog = AssetCatalog::open_project(root);
    ProbeGallery result{AssetId::parse("1f955498-5eaa-46a7-9958-9678072dc24b"), {}};
    const auto source = read(root / catalog.records().at(result.scene).source);
    EngineContext engine(WorldRole::Runtime, false);
    Scene scene(engine.world());
    scene.restore_snapshot(source);
    RuntimeSimulation simulation(engine.world(), scene);
    simulation.reset_presentation();
    const auto frame = extract_render_scene(simulation.presentation(1));
    check(frame.reflection_probes.size() == 2 && frame.meshes.size() == 4 &&
              frame.cameras.size() == 1 && frame.lights.size() == 1 && frame.diagnostics.empty(),
          "Local probe Gallery lost its authored geometry, maps or camera");
    std::set<int> priorities;
    for (unsigned index = 0; index < 2; ++index) {
        const auto& value = frame.reflection_probes[index].probe;
        result.maps[index] = value.texture.id;
        priorities.insert(value.priority);
        check(value.enabled && value.intensity == 2 && value.half_extent_x == 3 &&
                  value.half_extent_y == 3 && value.half_extent_z == 4 &&
                  value.blend_distance == 1 && value.rotation_degrees == 0 &&
                  value.box_projection &&
                  catalog.records().at(value.texture.id).type == TextureAsset::type,
              "Local probe Gallery changed its box, fade, priority or typed image reference");
    }
    check(result.maps[0] != result.maps[1] && priorities == std::set<int>{0, 1} &&
              read(root / catalog.records().at(result.scene).source) == source,
          "Local probe Gallery maps, ordering or source identities changed");
    std::sort(result.maps.begin(), result.maps.end());
    return result;
}
struct NormalGallery {
    AssetId scene, material, texture;
    auto operator<=>(const NormalGallery&) const = default;
};
NormalGallery inspect_normal_occlusion(const std::filesystem::path& root) {
    const auto catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
    NormalGallery result{AssetId::parse("277f080c-b97b-4202-bd31-5cded7e4dd18"),
                         AssetId::parse("7ed3592a-fab7-41d2-a5c4-c4c7c5eda387"),
                         AssetId::parse("45cb1cd6-c3ae-4f51-a7ff-314eea5cc26f")};
    const auto source = read(root / catalog->records().at(result.scene).source);
    EngineContext engine(WorldRole::Runtime, false);
    Scene scene(engine.world());
    scene.restore_snapshot(source);
    RuntimeSimulation simulation(engine.world(), scene);
    simulation.reset_presentation();
    const auto frame = extract_render_scene(simulation.presentation(1));
    check(frame.settings.ambient_occlusion == SceneAmbientOcclusion{true, 1.1f, 1, .01f, 1} &&
              frame.cameras.size() == 1 && frame.meshes.size() == 5 && frame.lights.size() == 1 &&
              frame.diagnostics.empty(),
          "Normal AO Gallery lost settings or geometry");
    unsigned mapped = 0;
    for (const auto& mesh : frame.meshes)
        for (const auto& slot : mesh.renderer.materials)
            mapped += slot.material.id == result.material;
    check(mapped == 1 && catalog->records().at(result.material).type == MaterialAsset::type &&
              catalog->records().at(result.texture).type == TextureAsset::type,
          "Normal AO Gallery lost its single typed mapped receiver");
    ResourcePool<MaterialAsset> pool;
    const auto ticket =
        asset_detail::request_model_pbr_material(pool, root, catalog, {result.material});
    check(pool.wait(ticket, std::chrono::seconds(5)) && bool(pool.acquire(ticket)),
          "Normal AO Gallery cooked material admission failed");
    const auto material = pool.acquire(ticket);
    const auto& texture = material->textures.at("normalTexture");
    check(texture.id == result.texture &&
              material->values.textures.at("normalTexture").semantic == TextureSemantic::Normal &&
              material->values.textures.at("normalTexture").scale == std::array<float, 2>{8, 4} &&
              scene.document() == source,
          "Normal AO Gallery changed mapped material values or authored Scene");
    return result;
}
} // namespace
int main(int argc, char** argv) {
    const char* stage = "copy and initial content inspection";
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
        const auto focus_scene = inspect_focus(root);
        const auto motion_scene = inspect_temporal(root, true);
        const auto occlusion_scene = inspect_occlusion(root);
        const auto reflection_scene = inspect_reflections(root);
        const auto rough_scene = inspect_reflections(root, true);
        const auto probes = inspect_local_probes(root);
        stage = "mapped normal content inspection";
        const auto normals = inspect_normal_occlusion(root);
        const std::array roots{source.at("asset_id").get<AssetId>(),
                               AssetId::parse("7456ed81-3d3b-45a6-8c7d-6b50073ad01a"),
                               exposure_scene,
                               fog_scene,
                               temporal_scene,
                               focus_scene,
                               motion_scene,
                               occlusion_scene,
                               reflection_scene,
                               probes.scene,
                               normals.scene,
                               rough_scene,
                               AssetId::parse("103bd727-b9c9-443d-90e1-5743c2094730")};
        {
            auto lease = std::make_shared<ProjectLease>(root);
            stage = "runtime catalog preparation";
            prepare_runtime_content_catalog(*lease, roots, {});
        }
        const auto package = root.parent_path() / (root.filename().string() + "-export");
        stage = "runtime content packaging";
        (void)package_runtime_content(root, package, roots, {"windows", "d3d12"});
        const auto moved = std::filesystem::path(package.string() + "-relocated");
        std::filesystem::rename(package, moved);
        const auto shipped = open_runtime_content(moved, {"windows", "d3d12"});
        std::set<AssetId> expected(roots.begin(), roots.end());
        expected.insert(materials.begin(), materials.end());
        expected.insert(probes.maps.begin(), probes.maps.end());
        expected.insert(normals.material);
        expected.insert(normals.texture);
        expected.insert(AssetId::parse("4ab91f02-df69-47c5-a626-3b6c7504e954"));
        expected.insert(AssetId::parse("68d22705-0b64-4773-9661-8c28465e0d98"));
        expected.insert(AssetId::parse("399c3c5e-f4e9-4ccb-94b3-bc0e518c1ece"));
        expected.insert(AssetId::parse("da50e938-f21b-4a87-b116-860bbbb7fdf3"));
        std::set<AssetId> actual;
        for (const auto& [id, record] : shipped.records()) {
            (void)record;
            actual.insert(id);
        }
        check(actual == expected && expected.size() == 23,
              "Bloom/color/exposure/fog export changed its exact shared asset closure");
        check(!std::filesystem::exists(moved / "Assets/Exposure/Daylight.animation.json"),
              "Exposure clip source leaked into the runtime package");
        for (const auto path : paths)
            check(!std::filesystem::exists(moved / path),
                  "Bloom material source leaked into export");
        stage = "source-free content inspection";
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
        check(inspect_focus(moved) == focus_scene,
              "Focus Gallery relocation changed its Scene identity or depth planes");
        check(inspect_temporal(moved, true) == motion_scene &&
                  !std::filesystem::exists(moved / "Assets/Physics2D/Platform.animation.json"),
              "Motion Gallery relocation changed identity, motion or leaked clip source");
        check(inspect_occlusion(moved) == occlusion_scene,
              "Ambient Gallery source-free relocation changed lighting or Scene identity");
        check(inspect_reflections(moved, true) == rough_scene,
              "Rough reflection Gallery source-free relocation changed settings or identity");
        check(inspect_reflections(moved) == reflection_scene,
              "Reflection Gallery source-free relocation changed material response or identity");
        const auto relocated_probes = inspect_local_probes(moved);
        check(relocated_probes.scene == probes.scene && relocated_probes.maps == probes.maps &&
                  !std::filesystem::exists(moved / "Assets/ReflectionProbes/WarmRoom.png") &&
                  !std::filesystem::exists(moved / "Assets/ReflectionProbes/CoolRoom.png"),
              "Local probe relocation changed map identity or copied editable image sources");
        check(inspect_normal_occlusion(moved) == normals &&
                  !std::filesystem::exists(moved / "Assets/OcclusionNormals/RippleNormals.png") &&
                  !std::filesystem::exists(moved /
                                           "Assets/OcclusionNormals/RippleWall.material.json"),
              "Normal AO Gallery relocation changed identities or leaked authoring sources");
        std::cout << "Gallery111 soft shadows, Gallery109 rough reflection and Gallery108 normal image,23-record "
                     "source-free closure PASS\n";
        std::cout << "Gallery107 two imported maps, boxes, priorities and shared source-free "
                     "closure PASS\n";
        std::cout
            << "Gallery106 reflective floor and typed lit materials, source-free relocation PASS\n";
        std::cout << "Gallery105 ambient lighting and five surfaces, source-free relocation PASS\n";
        std::cout << "Gallery104 motion settings,128 inherited animated cells,128 static cells and "
                     "source-free relocation PASS\n";
        std::cout
            << "Gallery103 focus settings, three depth planes and source-free relocation PASS\n";
        std::cout
            << "Gallery102 temporal settings, animated geometry and source-free relocation PASS\n";
        std::cout << "Gallery98 distance fog settings and source-free relocation PASS\n";
        std::cout
            << "Gallery48 runtime/cooked material/scene and relocated source-free export PASS\n";
        std::cout << "Gallery69 color settings and relocated source-free export PASS: "
                  << moved.string() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Gallery " << stage << ": " << error.what() << '\n';
        return 1;
    }
}
