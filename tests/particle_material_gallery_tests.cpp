#include "asset_bytes.hpp"
#include "asset_storage.hpp"
#include "gallery_asset_publication.hpp"
#include "material_authoring.hpp"
#include "particle_effect_authoring.hpp"
#include "particle_material_gallery_checks.hpp"
#include <algorithm>
#include <forge/engine_texture_assets.hpp>
#include <iostream>

using namespace forge;
using namespace forge::test_particle_material_gallery;
namespace {
void write(const std::filesystem::path& path, const Json& value) {
    std::filesystem::create_directories(path.parent_path());
    asset_storage::replace(path, value.dump(2) + "\n");
}
void author(const std::filesystem::path& root) {
    require(!std::filesystem::exists(root / scene_path), "Palette Gallery scene already exists");
    auto catalog = AssetCatalog::open_project(root);
    EngineContext engine;
    Scene scene(engine.world());
    const auto actor = [&](const std::string& name) {
        auto entity = scene.entity(authoring_command(scene, "entity.create", {{"name", name}})
                                       .at("selected")
                                       .get<std::string>());
        entity.set<Primitive>({no_primitive});
        return entity;
    };
    const std::array colors{std::array{.1, .8, .65, .55}, std::array{1., .45, .1, .45},
                            std::array{.7, .2, 1., .5}, std::array{.1, .4, 1., .6}};
    for (unsigned i = 0; i < names.size(); ++i) {
        const auto material_path = asset_path(i, ".material.json");
        const auto effect_path = asset_path(i, ".particles.json");
        require(!std::filesystem::exists(root / material_path) &&
                    !std::filesystem::exists(root / effect_path),
                "Palette assets already exist");
        auto material = MaterialSource::create(AssetId::generate());
        MaterialData values;
        values.model = "forge.gltf.unlit.v1";
        values.alpha = MaterialAlpha::Blend;
        values.depth_write = false;
        values.double_sided = true;
        values.parameters["baseColorFactor"] = {
            MaterialParameterType::LinearColor4,
            {float(colors[i][0]), float(colors[i][1]), float(colors[i][2]), float(colors[i][3])}};
        auto& slot = values.textures["baseColorTexture"];
        slot.semantic = TextureSemantic::Color;
        slot.offset = {.125f * i, -.0625f * i};
        slot.scale = {1.f + .25f * i, 1.f};
        slot.rotation = .125f * i;
        slot.sampler.min = slot.sampler.mag =
            i % 2 ? TextureFilter::Nearest : TextureFilter::Linear;
        slot.sampler.u = i > 1 ? TextureWrap::ClampEdge : TextureWrap::Repeat;
        const auto encoded = material_values_document(values);
        for (const auto* key : {"model", "alpha", "depth_write", "double_sided", "parameters"})
            material.document["overrides"][key] = encoded.at(key);
        material.document["overrides"]["textures"]["baseColorTexture"] = {
            {"asset", engine_texture(i % 2 ? EngineTexture::White : EngineTexture::Checker).id},
            {"slot", encoded.at("textures").at("baseColorTexture")}};
        material.validate();
        write(root / material_path, material.document);
        auto effect = ParticleEffectSource::create(AssetId::generate());
        effect.document["material"] = AssetRef<MaterialAsset>{material.asset()};
        effect.document["capacity"] = 64;
        effect.document["spawn_rate"] = 16;
        effect.document["lifetime"] = 4;
        effect.document["shape"] = "box";
        effect.document["shape_size"] = {2., 1.5, 1.};
        effect.document["acceleration"] = {0, 0, 0};
        effect.document["speed_min"] = .1;
        effect.document["speed_max"] = .2;
        effect.document["size_start"] = .45;
        effect.document["size_end"] = .15;
        effect.document["color_start"] = effect.document["color_end"] = {1, 1, 1};
        effect.enable_gpu_billboards(true);
        effect.enable_gpu_opacity(true);
        effect.enable_gpu_material_palette(true);
        effect.validate();
        write(root / effect_path, effect.document);
        catalog.add({material.asset(), MaterialAsset::type, material_path, 1, {}});
        catalog.add({effect.asset(), ParticleEffectAsset::type, effect_path, 1, {}});
        auto emitter = actor(std::string(names[i]) + " - distinct compatible Material");
        emitter.set<LocalTranslation>({float(i) * .6f - .9f, 1, float(i) * .3f});
        ParticleEmitter settings;
        settings.effect = {effect.asset()};
        settings.seed = 17 + 13 * i;
        emitter.set<ParticleEmitter>(settings);
    }
    auto camera = actor("Game Camera");
    camera.set<Camera>({});
    camera.set<LocalTranslation>({0, 1, -4.5f});
    auto light = actor("Directional light");
    light.set<Light>({});
    light.set<LocalRotation>({.3826834261f, 0, 0, .9238795042f});
    scene.save(root / scene_path);
    catalog.add_scene(scene_path);
    catalog.save(AssetCatalog::project_index(root));
}
AssetId inspect(const std::filesystem::path& root, bool mixed_graph = false) {
    const auto catalog = AssetCatalog::open_project(root);
    EngineContext engine;
    Scene scene(engine.world());
    scene.load(root / scene_path);
    const auto render = extract_render_scene(scene.effective_document());
    require(render.diagnostics.empty() && render.particles.size() == 4 &&
                render.cameras.size() == 1 && !render.settings.weighted_transparency,
            "Palette Gallery lost its four-emitter saved Scene");
    std::set<AssetId> selections;
    for (unsigned i = 0; i < names.size(); ++i) {
        const auto material = MaterialSource::parse(asset_detail::read_bytes(
            root / asset_path(i, ".material.json"), material_source_byte_limit));
        const auto resolved = resolve_material_source(material);
        const auto effect = ParticleEffectSource::parse(asset_detail::read_bytes(
            root / asset_path(i, ".particles.json"), particle_effect_byte_limit));
        const auto data = effect.compiled();
        require(data.version == (mixed_graph && i == 0 ? 49u : 48u) &&
                    bool(data.program) == (mixed_graph && i == 0) &&
                    particle_uses_gpu_material_palette(data) &&
                    data.material.id == material.asset() && data.mesh == engine_primitive(8) &&
                    resolved.values.model == "forge.gltf.unlit.v1" &&
                    resolved.values.alpha == MaterialAlpha::Blend && !resolved.values.depth_write,
                "Palette Gallery lost its explicit compatible device sources");
        require(catalog.records().at(effect.asset()).source == asset_path(i, ".particles.json") &&
                    catalog.records().at(material.asset()).source ==
                        asset_path(i, ".material.json"),
                "Palette Gallery source identity differs from its catalog");
        require(std::any_of(render.particles.begin(), render.particles.end(),
                            [&](const auto& emitter) {
                                return emitter.emitter.effect.id == effect.asset();
                            }),
                "Palette Gallery effect is absent from the Scene");
        selections.insert(material.asset());
    }
    require(selections.size() == 4 && catalog.records().at(scene.asset_id()).source == scene_path,
            "Palette Gallery lost distinct selections or Scene identity");
    return scene.asset_id();
}
void prepare(const std::filesystem::path& root, bool mixed_graph = false) {
    const auto id = inspect(root, mixed_graph);
    auto lease = std::make_shared<ProjectLease>(root);
    const auto target = package_target();
    const ImportTarget import{target.platform, target.backend,
                              target.platform == "windows" ? "desktop" : "cpu"};
    AssetImportService materials(lease, material_import_registry(), import);
    AssetImportService effects(lease, particle_effect_import_registry(), import);
    for (unsigned i = 0; i < names.size(); ++i) {
        test::publish_gallery_asset(materials, asset_path(i, ".material.json"),
                                    prepare_material_publication);
        test::publish_gallery_asset(effects, asset_path(i, ".particles.json"),
                                    prepare_particle_effect_publication);
    }
    EngineContext engine;
    Scene scene(engine.world());
    scene.load(root / scene_path);
    const std::array roots{id};
    prepare_runtime_content_catalog(*lease, roots, {}, scene.schema());
}
void configure_graph(const std::filesystem::path& root) {
    const auto original = ParticleEffectSource::parse(asset_detail::read_bytes(
        root / "Assets/Particles/Graphs/OrbitFountain.particles.json", particle_effect_byte_limit));
    const auto program = original.graph_program();
    require(bool(program), "The published Gallery Graph program is missing");
    auto lease = std::make_shared<ProjectLease>(root);
    ParticleEffectDocument effect(lease, asset_path(0, ".particles.json"));
    const auto identity = effect.source().asset();
    effect.edit_source(effect.revision(), "Use published Gallery Graph",
                       [&](auto& source) { source.set_graph_program(*program); });
    require(effect.source().asset() == identity && effect.source().compiled().version == 49,
            "Mixed palette Graph configuration changed its identity or explicit profile");
    effect.save();
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc >= 3, "Provide palette Gallery action and project root");
        const auto root = std::filesystem::absolute(argv[2]);
        const std::string_view action = argv[1];
        if (action == "--author")
            author(root);
        else if (action == "--inspect")
            (void)inspect(root);
        else if (action == "--prepare")
            prepare(root);
        else if (action == "--verify-export" || action == "--verify-graph-export") {
            const bool mixed_graph = action == "--verify-graph-export";
            require(argc == 4, "Provide private palette export scratch");
            const auto owner = std::filesystem::absolute(argv[3]) / AssetId::generate().str();
            const auto source = owner / "source";
            std::filesystem::create_directories(owner);
            std::filesystem::copy(root, source, std::filesystem::copy_options::recursive);
            if (mixed_graph)
                configure_graph(source);
            prepare(source, mixed_graph);
            const std::array roots{inspect(source, mixed_graph)};
            (void)package_runtime_content(source, owner / "content", roots, package_target());
            std::filesystem::rename(source, owner / "source-offline");
            std::filesystem::rename(owner / "content", owner / "relocated-content");
            (void)game(owner / "relocated-content", mixed_graph);
            std::cout << "Palette source-hidden relocation/World passed: four distinct Materials, "
                         "device birth windows and unchanged authored Scene\n";
        } else
            throw std::runtime_error("Unknown palette Gallery action");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
