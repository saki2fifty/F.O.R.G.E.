#include "asset_bytes.hpp"
#include "asset_storage.hpp"
#include "gallery_asset_publication.hpp"
#include "material_authoring.hpp"
#include "material_selection.hpp"
#include "particle_effect_authoring.hpp"
#include "particle_effect_resource.hpp"
#include "particle_runtime.hpp"
#include "runtime_package.hpp"
#include <algorithm>
#include <forge/authoring.hpp>
#include <forge/engine_assets.hpp>
#include <forge/primitive_catalog.hpp>
#include <forge/render_scene.hpp>
#include <forge/runtime.hpp>
#include <iostream>
#include <set>
using namespace forge;
namespace {
struct Example {
    const char* scene;
    const char* effect;
    bool opacity;
};
constexpr Example alpha{"Scenes/240-sorted-alpha-particles.scene.json",
                        "Assets/Particles/Alpha/GlassCloud.particles.json", false};
constexpr Example fading{"Scenes/241-particle-opacity.scene.json",
                         "Assets/Particles/Alpha/FadingCloud.particles.json", true};
constexpr const char* material_path = "Assets/Particles/Alpha/Glass.material.json";
void check(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
void write(const std::filesystem::path& path, const Json& value) {
    std::filesystem::create_directories(path.parent_path());
    asset_storage::replace(path, value.dump(2) + "\n");
}
RuntimePackageTarget target() {
#ifdef _WIN32
    return {"windows", "d3d12"};
#else
    return {"linux", "none"};
#endif
}
void author(const std::filesystem::path& root, const Example& example) {
    for (const auto* path : {example.scene, example.effect})
        check(!std::filesystem::exists(root / path), "Sorted-alpha Gallery source already exists");
    check(example.opacity == std::filesystem::exists(root / material_path),
          "Alpha Material must already exist only for the fading example");
    auto material = example.opacity ? MaterialSource::parse(asset_detail::read_bytes(
                                          root / material_path, material_source_byte_limit))
                                    : MaterialSource::create(AssetId::generate());
    if (!example.opacity) {
        auto& overrides = material.document["overrides"];
        overrides["model"] = "forge.gltf.unlit.v1";
        overrides["alpha"] = unsigned(MaterialAlpha::Blend);
        overrides["depth_test"] = true;
        overrides["depth_write"] = false;
        overrides["parameters"]["baseColorFactor"] = {
            {"type", unsigned(MaterialParameterType::LinearColor4)}, {"value", {1, 1, 1, .4}}};
        material.validate();
        write(root / material_path, material.document);
    }
    auto effect = ParticleEffectSource::create(AssetId::generate());
    effect.set_evaluation(ParticleEvaluation::CpuAlpha);
    effect.document["material"] = AssetRef<MaterialAsset>{material.asset()};
    effect.document["capacity"] = 128;
    effect.document["spawn_rate"] = 32;
    effect.document["lifetime"] = 4;
    effect.document["shape"] = "box";
    effect.document["shape_size"] = {2.5, 1.5, 2};
    effect.document["speed_min"] = .2;
    effect.document["speed_max"] = .5;
    effect.document["acceleration"] = {0, 0, 0};
    effect.document["size_start"] = .35;
    effect.document["size_end"] = .1;
    effect.document["color_start"] = effect.document["color_end"] = {1, 1, 1};
    if (example.opacity) {
        effect.enable_opacity(true);
        effect.document["opacity_start"] = effect.document["opacity_end"] = 0;
        effect.enable_lifetime(ParticleLifetimeChannel::Opacity, true);
        effect.insert_lifetime_stop(ParticleLifetimeChannel::Opacity, .15);
        effect.document["opacity_curve"][1]["value"] = 1;
    }
    effect.validate();
    write(root / example.effect, effect.document);
    auto catalog = AssetCatalog::open_project(root);
    if (!example.opacity)
        catalog.add({material.asset(), MaterialAsset::type, material_path, 1, {}});
    catalog.add({effect.asset(), ParticleEffectAsset::type, example.effect, 1, {}});
    catalog.save(AssetCatalog::project_index(root));
    EngineContext engine;
    Scene scene(engine.world());
    auto actor = [&](const char* name) {
        auto entity = scene.entity(authoring_command(scene, "entity.create", {{"name", name}})
                                       .at("selected")
                                       .get<std::string>());
        entity.set<Primitive>({no_primitive});
        return entity;
    };
    auto teal = actor(example.opacity ? "Teal fade - shared opacity effect"
                                      : "Teal glass - shared sorted-alpha effect");
    teal.set<LocalTranslation>({-.7f, 1, .5f});
    ParticleEmitter emitter;
    emitter.effect = {effect.asset()};
    emitter.seed = 41;
    emitter.tint_r = .15f;
    emitter.tint_g = .8f;
    teal.set<ParticleEmitter>(emitter);
    auto amber = actor(example.opacity ? "Amber fade - shared opacity effect"
                                       : "Amber glass - shared sorted-alpha effect");
    amber.set<LocalTranslation>({.7f, 1, -.5f});
    emitter.seed = 71;
    emitter.tint_r = 1;
    emitter.tint_g = .4f;
    emitter.tint_b = .1f;
    amber.set<ParticleEmitter>(emitter);
    auto marker = actor("Opaque depth marker");
    MeshRenderer mesh;
    mesh.mesh = engine_primitive(0);
    marker.set<MeshRenderer>(mesh);
    marker.set<LocalTranslation>({0, 1, -.75f});
    marker.set<LocalScale>({.2f, 2, .2f});
    auto camera = actor("Game Camera");
    camera.set<Camera>({});
    camera.set<LocalTranslation>({0, 2, -9});
    auto light = actor("Directional light");
    light.set<Light>({});
    light.set<LocalRotation>({.3826834261f, 0, 0, .9238795042f});
    scene.save(root / example.scene);
    catalog = AssetCatalog::open_project(root);
    catalog.add_scene(example.scene);
    catalog.save(AssetCatalog::project_index(root));
}
std::array<AssetId, 3> inspect(const std::filesystem::path& root, const Example& example) {
    const auto material = MaterialSource::parse(
        asset_detail::read_bytes(root / material_path, material_source_byte_limit));
    const auto resolved = resolve_material_source(material);
    check(resolved.values.model == "forge.gltf.unlit.v1" &&
              resolved.values.alpha == MaterialAlpha::Blend && resolved.values.depth_test &&
              !resolved.values.depth_write,
          "Sorted-alpha example lost its reusable transparent Material");
    const auto effect = ParticleEffectSource::parse(
        asset_detail::read_bytes(root / example.effect, particle_effect_byte_limit));
    check(effect.compiled().version == (example.opacity ? 6u : 5u) &&
              effect.compiled().material.id == material.asset() && !effect.compiled().program &&
              effect.compiled().capacity == 128,
          "Sorted-alpha example lost CPU evaluation or its shared Material");
    if (example.opacity)
        check(effect.compiled().opacity_curve.size() == 3 &&
                  effect.compiled().opacity_curve[0].value == 0 &&
                  effect.compiled().opacity_curve[1].time == .15 &&
                  effect.compiled().opacity_curve[1].value == 1 &&
                  effect.compiled().opacity_curve[2].value == 0,
              "Fading example lost its editable birth/fade/death opacity curve");
    EngineContext engine;
    Scene scene(engine.world());
    scene.load(root / example.scene);
    const auto render = extract_render_scene(scene.effective_document());
    check(render.diagnostics.empty() && render.particles.size() == 2 && render.meshes.size() == 1 &&
              render.cameras.size() == 1 && render.lights.size() == 1,
          "Sorted-alpha example lost its native Scene consumers");
    for (const auto& emitter : render.particles)
        check(emitter.emitter.effect.id == effect.asset(), "Example duplicated an effect");
    const auto catalog = AssetCatalog::open_project(root);
    check(catalog.records().at(material.asset()).source == material_path &&
              catalog.records().at(effect.asset()).source == example.effect &&
              catalog.records().at(scene.asset_id()).source == example.scene,
          "Sorted-alpha example identity/catalog mismatch");
    return {scene.asset_id(), effect.asset(), material.asset()};
}
void prepare(const std::filesystem::path& root, const Example& example,
             bool publish_material = true) {
    const auto ids = inspect(root, example);
    const auto runtime = target();
    const ImportTarget import{runtime.platform, runtime.backend,
                              runtime.platform == "windows" ? "desktop" : "cpu"};
    auto lease = std::make_shared<ProjectLease>(root);
    AssetImportService materials(lease, material_import_registry(), import);
    if (publish_material)
        test::publish_gallery_asset(materials, material_path, prepare_material_publication);
    AssetImportService particles(lease, particle_effect_import_registry(), import);
    test::publish_gallery_asset(particles, example.effect, prepare_particle_effect_publication);
    EngineContext engine;
    Scene scene(engine.world());
    scene.load(root / example.scene);
    const std::array roots{ids[0]};
    prepare_runtime_content_catalog(*lease, roots, {}, scene.schema());
}
void verify_export(const std::filesystem::path& root, const Example& example) {
    const auto ids = inspect(root, example);
    prepare(root, example);
    const auto output = root.parent_path() / "content";
    const auto relocated = root.parent_path() / "relocated-content";
    const std::array roots{ids[0]};
    (void)package_runtime_content(root, output, roots, target());
    std::filesystem::rename(root, root.parent_path() / "source-offline");
    std::filesystem::rename(output, relocated);
    const auto catalog = open_runtime_content(relocated, target());
    std::set<AssetId> actual;
    for (const auto& [id, record] : catalog.records()) {
        (void)record;
        actual.insert(id);
    }
    check(actual == std::set<AssetId>(ids.begin(), ids.end()) &&
              !std::filesystem::exists(relocated / material_path) &&
              !std::filesystem::exists(relocated / example.effect),
          "Sorted-alpha relocation lost its exact source-hidden closure");
    ResourcePool<ParticleEffectAsset> effects;
    const auto ticket = request_particle_effect(
        effects, relocated, std::make_shared<const AssetCatalog>(catalog), {ids[1]});
    check(effects.wait(ticket, std::chrono::seconds(15)), "Relocated alpha effect load timed out");
    const auto selected = effects.acquire(ticket);
    check(bool(selected) && selected->version == (example.opacity ? 6u : 5u) &&
              selected->material.id == ids[2],
          "Relocated source-free alpha effect has different values");
    const auto material = asset_detail::load_material_selection(relocated, catalog, {ids[2]});
    check(material.data.values.alpha == MaterialAlpha::Blend && !material.data.values.depth_write,
          "Relocated source-free alpha Material has different values");
    EngineContext runtime(WorldRole::Runtime, false, {particle_module(relocated)});
    Scene scene(runtime.world());
    scene.load(relocated / catalog.records().at(ids[0]).source);
    const auto saved = scene.document();
    RuntimeSimulation simulation(runtime.world(), scene);
    const auto particles = particle_runtime(runtime.world());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!particles->prepare()) {
        check(std::chrono::steady_clock::now() < deadline, "Source-free alpha World timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    simulation.reset_presentation();
    for (unsigned i = 0; i < 12; ++i)
        simulation.tick(.25f);
    const auto render = extract_render_scene(simulation.presentation(1));
    check(render.diagnostics.empty() && render.particles.size() == 2 && scene.document() == saved,
          "Source-free alpha World lost its compact emitters or mutated authored values");
    for (const auto& emitter : render.particles) {
        const auto samples = particles->sample(scene.entity(emitter.entity.str()).id(), 1);
        check(emitter.time == 3 && samples.size() == 96,
              "Source-free alpha emitter lost fixed-clock CPU birth membership");
        if (example.opacity) {
            const auto [minimum, maximum] = std::minmax_element(
                samples.begin(), samples.end(),
                [](const auto& a, const auto& b) { return a.opacity < b.opacity; });
            check(minimum->opacity >= 0 && minimum->opacity < .1 && maximum->opacity > .9 &&
                      maximum->opacity <= 1,
                  "Source-free World did not evaluate independent per-birth opacity");
        }
    }
    std::cout << "CPU sorted-alpha publication/relocation and fixed-clock World passed\n";
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc >= 3, "Need sorted-alpha Gallery action and project root");
        const auto root = std::filesystem::absolute(argv[2]);
        const std::string_view action = argv[1];
        if (action == "--author" || action == "--author-opacity")
            author(root, action == "--author" ? alpha : fading);
        else if (action == "--inspect" || action == "--inspect-opacity")
            (void)inspect(root, action == "--inspect" ? alpha : fading);
        else if (action == "--prepare") {
            prepare(root, alpha);
            if (std::filesystem::exists(root / fading.effect))
                prepare(root, fading, false);
        } else if (action == "--prepare-opacity") {
            // Author the new Effect against the already selected shared Material.
            // Full staging/relocation above republishes the complete target closure.
            prepare(root, fading, false);
        } else if (action == "--verify-export" || action == "--verify-export-opacity") {
            check(argc == 4, "Need private verification scratch root");
            const auto copy =
                std::filesystem::absolute(argv[3]) / AssetId::generate().str() / "source";
            std::filesystem::create_directories(copy.parent_path());
            std::filesystem::copy(root, copy, std::filesystem::copy_options::recursive);
            verify_export(copy, action == "--verify-export" ? alpha : fading);
        } else
            throw std::runtime_error("Unknown sorted-alpha Gallery action");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
