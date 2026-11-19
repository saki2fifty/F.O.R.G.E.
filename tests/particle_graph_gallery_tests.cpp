#include "asset_bytes.hpp"
#include "asset_storage.hpp"
#include "bounded_json.hpp"
#include "gallery_asset_publication.hpp"
#include "particle_effect_authoring.hpp"
#include "particle_effect_resource.hpp"
#include "particle_graph_example.hpp"
#include "runtime_package.hpp"
#include <forge/authoring.hpp>
#include <forge/primitive_catalog.hpp>
#include <forge/render_scene.hpp>
#include <forge/shader_resource.hpp>
#ifdef FORGE_GALLERY_SHADER_COOK
#include "shader_authoring.hpp"
#include "shader_diligent.hpp"
#include "shader_pipeline.hpp"
#endif
#include <fstream>
#include <iostream>
#include <set>
using namespace forge;
namespace {
constexpr const char* scene_path = "Scenes/239-particle-graphs.scene.json";
constexpr const char* shader_path = "Assets/Particles/Graphs/Orbit.graph.shader.json";
constexpr const char* effect_path = "Assets/Particles/Graphs/OrbitFountain.particles.json";
void check(bool ok, const std::string& why) {
    if (!ok)
        throw std::runtime_error(why);
}
Json read(const std::filesystem::path& path) {
    return asset_detail::parse_bounded_json(asset_detail::read_bytes(path, 1024 * 1024),
                                            1024 * 1024);
}
void write(const std::filesystem::path& path, const Json& value) {
    std::filesystem::create_directories(path.parent_path());
    asset_storage::replace(path, value.dump(2) + "\n");
}
void author(const std::filesystem::path& root) {
    for (const auto* path : {scene_path, shader_path, effect_path})
        check(!std::filesystem::exists(root / path), "Particle graph example already exists");
    auto catalog = AssetCatalog::open_project(root);
    const auto shader = test::particle_graph_example(AssetId::generate());
    write(root / shader_path, shader.document);
    auto effect = ParticleEffectSource::create(AssetId::generate());
    effect.document["capacity"] = 512;
    effect.document["spawn_rate"] = 96;
    effect.document["lifetime"] = 3.5;
    effect.document["cone_angle"] = 12;
    effect.document["speed_min"] = 1.3;
    effect.document["speed_max"] = 2.2;
    effect.document["acceleration"] = {0, -.7, 0};
    effect.document["drag"] = .3;
    effect.document["shape"] = "disc";
    effect.document["shape_radius"] = .35;
    effect.document["size_start"] = .12;
    effect.document["size_end"] = .04;
    effect.document["color_start"] = {1, .22, .03};
    effect.document["color_end"] = {.03, .2, .3};
    effect.enable_lifetime(ParticleLifetimeChannel::Size, true);
    const auto stop = effect.insert_lifetime_stop(ParticleLifetimeChannel::Size, .35);
    for (auto& row : effect.document["size_curve"])
        if (row.at("id").get<AssetId>() == stop)
            row["value"] = .18;
    effect.enable_lifetime(ParticleLifetimeChannel::Color, true);
    effect.set_graph_program({{shader.asset()}, {2.5, 4, 2.5}, .25});
    write(root / effect_path, effect.document);
    catalog.add({shader.asset(), ShaderAsset::type, shader_path, 1, {}});
    catalog.add({effect.asset(), ParticleEffectAsset::type, effect_path, 1, {}});
    catalog.save(AssetCatalog::project_index(root));
    EngineContext engine;
    Scene scene(engine.world());
    const auto create = [&](const char* name) {
        auto actor = scene.entity(authoring_command(scene, "entity.create", {{"name", name}})
                                      .at("selected")
                                      .get<std::string>());
        actor.set<Primitive>({no_primitive});
        return actor;
    };
    auto first = create("Orbit fountain - shared graph");
    first.set<LocalTranslation>({-3, 0, 0});
    ParticleEmitter emitter;
    emitter.effect = {effect.asset()};
    emitter.seed = 23;
    first.set<ParticleEmitter>(emitter);
    auto second = create("Orbit fountain - larger blue tint");
    second.set<LocalTranslation>({3, 0, 0});
    emitter.seed = 71;
    emitter.size_multiplier = 1.5f;
    emitter.tint_r = .3f;
    emitter.tint_g = .7f;
    second.set<ParticleEmitter>(emitter);
    auto camera = create("Game Camera");
    camera.set<Camera>({});
    camera.set<LocalTranslation>({0, 6, 15});
    camera.set<LocalRotation>({0, .9816271663f, -.1908089966f, 0});
    auto light = create("Directional light");
    light.set<Light>({});
    light.set<LocalRotation>({.3826834261f, 0, 0, .9238795042f});
    scene.save(root / scene_path);
    catalog = AssetCatalog::open_project(root);
    catalog.add_scene(scene_path);
    catalog.save(AssetCatalog::project_index(root));
}
void inspect(const std::filesystem::path& root) {
    const auto source =
        MaterialGraphSource::parse(asset_detail::read_bytes(root / shader_path, 1024 * 1024));
    const auto program = shader_program_source(source.document);
    check(source.document.at("version") == 4 && program.particle_graph_profile == 1 &&
              program.stages.size() == 1 && program.stages.front().stage == ShaderStage::Compute,
          "Gallery particle graph lost its generated Compute profile");
    check(source.document.at("graph").at("functions").size() == 1,
          "Gallery particle graph lost its reusable pure function");
    const auto effect = ParticleEffectSource::parse(
        asset_detail::read_bytes(root / effect_path, particle_effect_byte_limit));
    const auto values = effect.compiled();
    check(values.version == 4 && values.program && values.program->shader.id == source.asset() &&
              values.capacity == 512 && values.size_curve.size() == 3,
          "Gallery graph effect lost its Shader, capacity or lifetime values");
    EngineContext engine;
    Scene scene(engine.world());
    scene.load(root / scene_path);
    const auto snapshot = extract_render_scene(scene.effective_document());
    check(snapshot.diagnostics.empty() && snapshot.cameras.size() == 1 &&
              snapshot.lights.size() == 1 && snapshot.particles.size() == 2 &&
              snapshot.meshes.empty(),
          "Gallery particle graph Scene lost its native render consumers");
    for (const auto& emitter : snapshot.particles)
        check(emitter.emitter.effect.id == effect.asset(),
              "Gallery emitters do not share the graph effect");
    const auto catalog = AssetCatalog::open_project(root);
    check(catalog.records().at(source.asset()).source == shader_path &&
              catalog.records().at(effect.asset()).source == effect_path &&
              catalog.records().at(scene.asset_id()).source == scene_path,
          "Gallery particle graph logical identities differ from the catalog");
}
void prepare(const std::filesystem::path& root, const std::filesystem::path& worker) {
#ifdef FORGE_GALLERY_SHADER_COOK
    inspect(root);
    auto lease = std::make_shared<ProjectLease>(root);
    auto registry = std::make_shared<AssetImporterRegistry>();
    registry->add(asset_detail::shader_importer(worker.parent_path() / "forge_shader_build.exe",
                                                {asset_detail::diligent_shader_compiler_digest(),
                                                 asset_detail::diligent_shader_compiler_debug()}));
    registry->seal();
    AssetImportService shaders(lease, registry, {"windows-x64", "d3d12", "fxc-5.1"});
    test::publish_gallery_asset(
        shaders, shader_path, prepare_shader_publication,
        std::chrono::seconds(asset_detail::shader_worker_limits().seconds + 15));
    AssetImportService particles(lease, particle_effect_import_registry(),
                                 {"windows", "d3d12", "desktop"});
    test::publish_gallery_asset(particles, effect_path, prepare_particle_effect_publication);
    EngineContext engine;
    Scene scene(engine.world());
    scene.load(root / scene_path);
    const std::array roots{scene.asset_id()};
    prepare_runtime_content_catalog(*lease, roots, {}, scene.schema());
#else
    (void)root;
    (void)worker;
    throw std::runtime_error("Particle graph cooking requires the Windows Shader worker");
#endif
}
void verify_export(const std::filesystem::path& root, const std::filesystem::path& worker) {
    prepare(root, worker);
    const auto scene = read(root / scene_path).at("asset_id").get<AssetId>();
    const auto shader = read(root / shader_path).at("asset_id").get<AssetId>();
    const auto effect = read(root / effect_path).at("asset_id").get<AssetId>();
    const auto output = root.parent_path() / "content",
               relocated = root.parent_path() / "relocated-content";
    const std::array roots{scene};
    (void)package_runtime_content(root, output, roots, {"windows", "d3d12"});
    std::filesystem::rename(root, root.parent_path() / "source-offline");
    std::filesystem::rename(output, relocated);
    const auto catalog = open_runtime_content(relocated, {"windows", "d3d12"});
    std::set<AssetId> actual;
    for (const auto& [id, record] : catalog.records()) {
        (void)record;
        actual.insert(id);
    }
    check(actual == std::set<AssetId>{scene, shader, effect} &&
              !std::filesystem::exists(relocated / shader_path) &&
              !std::filesystem::exists(relocated / effect_path),
          "Particle graph export lost its exact source-hidden content closure");
    ResourcePool<ShaderAsset> shaders;
    const auto ticket = request_shader(shaders, relocated, catalog, {shader});
    check(shaders.wait(ticket, std::chrono::seconds(10)),
          "Relocated particle Shader failed to load");
    const auto lease = shaders.acquire(ticket);
    check(bool(lease), "Relocated particle Shader lease missing");
    validate_particle_graph_shader(lease.get());
    std::cout << "Particle graph Windows cook and relocated source-hidden Shader closure passed\n";
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc >= 3, "Need particle graph Gallery action/root");
        const std::string action = argv[1];
        const auto root = std::filesystem::absolute(argv[2]);
        if (action == "--author")
            author(root);
        else if (action == "--inspect")
            inspect(root);
        else if (action == "--prepare" || action == "--verify-export") {
            check(argc == 4, "Need the matching Windows worker directory");
            if (action == "--prepare")
                prepare(root, argv[3]);
            else
                verify_export(root, argv[3]);
        } else
            throw std::runtime_error("Unknown particle graph Gallery action");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
