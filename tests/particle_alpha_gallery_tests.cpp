#include "asset_bytes.hpp"
#include "asset_storage.hpp"
#include "gallery_asset_publication.hpp"
#include "material_authoring.hpp"
#include "material_selection.hpp"
#include "particle_effect_authoring.hpp"
#include "particle_effect_resource.hpp"
#include "particle_runtime.hpp"
#include "runtime_package.hpp"
#include <forge/authoring.hpp>
#include <forge/engine_assets.hpp>
#include <forge/primitive_catalog.hpp>
#include <forge/render_scene.hpp>
#include <forge/runtime.hpp>
#include <iostream>
#include <set>
using namespace forge;
namespace {
constexpr const char* scene_path = "Scenes/240-sorted-alpha-particles.scene.json";
constexpr const char* effect_path = "Assets/Particles/Alpha/GlassCloud.particles.json";
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
void author(const std::filesystem::path& root) {
    for (const auto* path : {scene_path, effect_path, material_path})
        check(!std::filesystem::exists(root / path), "Sorted-alpha Gallery source already exists");
    auto material = MaterialSource::create(AssetId::generate());
    auto& overrides = material.document["overrides"];
    overrides["model"] = "forge.gltf.unlit.v1";
    overrides["alpha"] = unsigned(MaterialAlpha::Blend);
    overrides["depth_test"] = true;
    overrides["depth_write"] = false;
    overrides["parameters"]["baseColorFactor"] = {
        {"type", unsigned(MaterialParameterType::LinearColor4)}, {"value", {1, 1, 1, .4}}};
    material.validate();
    write(root / material_path, material.document);
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
    effect.validate();
    write(root / effect_path, effect.document);
    auto catalog = AssetCatalog::open_project(root);
    catalog.add({material.asset(), MaterialAsset::type, material_path, 1, {}});
    catalog.add({effect.asset(), ParticleEffectAsset::type, effect_path, 1, {}});
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
    auto teal = actor("Teal glass - shared sorted-alpha effect");
    teal.set<LocalTranslation>({-.7f, 1, .5f});
    ParticleEmitter emitter;
    emitter.effect = {effect.asset()};
    emitter.seed = 41;
    emitter.tint_r = .15f;
    emitter.tint_g = .8f;
    teal.set<ParticleEmitter>(emitter);
    auto amber = actor("Amber glass - shared sorted-alpha effect");
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
    scene.save(root / scene_path);
    catalog = AssetCatalog::open_project(root);
    catalog.add_scene(scene_path);
    catalog.save(AssetCatalog::project_index(root));
}
std::array<AssetId, 3> inspect(const std::filesystem::path& root) {
    const auto material = MaterialSource::parse(
        asset_detail::read_bytes(root / material_path, material_source_byte_limit));
    const auto resolved = resolve_material_source(material);
    check(resolved.values.model == "forge.gltf.unlit.v1" &&
              resolved.values.alpha == MaterialAlpha::Blend && resolved.values.depth_test &&
              !resolved.values.depth_write,
          "Sorted-alpha example lost its reusable transparent Material");
    const auto effect = ParticleEffectSource::parse(
        asset_detail::read_bytes(root / effect_path, particle_effect_byte_limit));
    check(effect.compiled().version == 5 && effect.compiled().material.id == material.asset() &&
              !effect.compiled().program && effect.compiled().capacity == 128,
          "Sorted-alpha example lost CPU evaluation or its shared Material");
    EngineContext engine;
    Scene scene(engine.world());
    scene.load(root / scene_path);
    const auto render = extract_render_scene(scene.effective_document());
    check(render.diagnostics.empty() && render.particles.size() == 2 && render.meshes.size() == 1 &&
              render.cameras.size() == 1 && render.lights.size() == 1,
          "Sorted-alpha example lost its native Scene consumers");
    for (const auto& emitter : render.particles)
        check(emitter.emitter.effect.id == effect.asset(), "Example duplicated an effect");
    const auto catalog = AssetCatalog::open_project(root);
    check(catalog.records().at(material.asset()).source == material_path &&
              catalog.records().at(effect.asset()).source == effect_path &&
              catalog.records().at(scene.asset_id()).source == scene_path,
          "Sorted-alpha example identity/catalog mismatch");
    return {scene.asset_id(), effect.asset(), material.asset()};
}
void prepare(const std::filesystem::path& root) {
    const auto ids = inspect(root);
    const auto runtime = target();
    const ImportTarget import{runtime.platform, runtime.backend,
                              runtime.platform == "windows" ? "desktop" : "cpu"};
    auto lease = std::make_shared<ProjectLease>(root);
    AssetImportService materials(lease, material_import_registry(), import);
    test::publish_gallery_asset(materials, material_path, prepare_material_publication);
    AssetImportService particles(lease, particle_effect_import_registry(), import);
    test::publish_gallery_asset(particles, effect_path, prepare_particle_effect_publication);
    EngineContext engine;
    Scene scene(engine.world());
    scene.load(root / scene_path);
    const std::array roots{ids[0]};
    prepare_runtime_content_catalog(*lease, roots, {}, scene.schema());
}
void verify_export(const std::filesystem::path& root) {
    const auto ids = inspect(root);
    prepare(root);
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
              !std::filesystem::exists(relocated / effect_path),
          "Sorted-alpha relocation lost its exact source-hidden closure");
    ResourcePool<ParticleEffectAsset> effects;
    const auto ticket = request_particle_effect(
        effects, relocated, std::make_shared<const AssetCatalog>(catalog), {ids[1]});
    check(effects.wait(ticket, std::chrono::seconds(15)), "Relocated alpha effect load timed out");
    const auto selected = effects.acquire(ticket);
    check(bool(selected) && selected->version == 5 && selected->material.id == ids[2],
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
    }
    std::cout << "CPU sorted-alpha publication/relocation and fixed-clock World passed\n";
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc >= 3, "Need sorted-alpha Gallery action and project root");
        const auto root = std::filesystem::absolute(argv[2]);
        const std::string_view action = argv[1];
        if (action == "--author")
            author(root);
        else if (action == "--inspect")
            (void)inspect(root);
        else if (action == "--prepare")
            prepare(root);
        else if (action == "--verify-export") {
            check(argc == 4, "Need private verification scratch root");
            const auto copy =
                std::filesystem::absolute(argv[3]) / AssetId::generate().str() / "source";
            std::filesystem::create_directories(copy.parent_path());
            std::filesystem::copy(root, copy, std::filesystem::copy_options::recursive);
            verify_export(copy);
        } else
            throw std::runtime_error("Unknown sorted-alpha Gallery action");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
