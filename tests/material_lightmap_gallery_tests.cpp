#include "asset_bytes.hpp"
#include "asset_storage.hpp"
#include "material_authoring.hpp"
#include "material_lightmap_fixture.hpp"
#include "model_render_resource.hpp"
#include "runtime_package.hpp"
#include "texture_authoring.hpp"
#ifdef FORGE_LIGHTMAP_COOK
#include "shader_authoring.hpp"
#include "shader_diligent.hpp"
#include "shader_pipeline.hpp"
#endif
#include <forge/authoring.hpp>
#include <forge/engine_assets.hpp>
#include <forge/material_graph.hpp>
#include <forge/primitive_catalog.hpp>
#include <forge/render_components.hpp>
#include <forge/render_scene.hpp>
#include <fstream>
#include <iostream>
using namespace forge;
namespace {
constexpr auto scene_path = "Scenes/158-imported-lightmaps.scene.json";
constexpr auto shader_path = "Assets/Lightmaps/Irradiance.shader.json";
constexpr auto material_path = "Assets/Lightmaps/Shared.material.json";
constexpr auto image_path = "Assets/Lightmaps/Example.hdr";
void check(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
#ifdef FORGE_LIGHTMAP_COOK
Json read(const std::filesystem::path& path) {
    Json value;
    std::ifstream(path) >> value;
    return value;
}
#endif
template <class Prepare>
void publish(AssetImportService& service, const std::filesystem::path& source, Prepare prepare) {
    service.submit(
        service.prepare(source),
        [&](auto& candidate, const auto& plan, const auto&) { prepare(candidate, plan); },
        [](const auto&, const auto&) {});
    check(service.wait_idle(std::chrono::seconds(90)), "Lightmap Gallery publication timed out");
    const auto results = service.poll();
    check(results.size() == 1 && results[0].published,
          "Lightmap Gallery publication failed: " +
              (results.empty() ? std::string("missing result") : results[0].diagnostic));
}
void author(const std::filesystem::path& root, const std::filesystem::path& worker) {
    check(!std::filesystem::exists(root / scene_path), "Lightmap Gallery already exists");
    std::filesystem::create_directories(root / "Assets/Lightmaps");
    // Original illustrative incident irradiance, not the result of a Scene bake.
    // A four-pixel scanline uses Radiance's ordinary non-RLE RGBE representation.
    const std::array<std::array<unsigned char, 4>, 4> colors{
        {{{128, 32, 16, 129}}, {{32, 128, 16, 129}}, {{16, 32, 128, 130}}, {{128, 96, 24, 131}}}};
    {
        std::ofstream image(root / image_path, std::ios::binary);
        image << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 4 +X 4\n";
        for (unsigned y = 0; y < 4; ++y)
            for (unsigned x = 0; x < 4; ++x) {
                const auto& color = colors[(x / 2) + 2 * (y / 2)];
                image.write(reinterpret_cast<const char*>(color.data()), color.size());
            }
        check(bool(image), "Lightmap illustrative image write failed");
    }
    auto lease = std::make_shared<ProjectLease>(root);
    AssetImportService textures(lease, texture_import_registry(worker), desktop_texture_target());
    publish(textures, image_path, prepare_texture_publication);
    auto catalog = AssetCatalog::open_project(root);
    AssetId texture;
    for (const auto& [id, record] : catalog.records())
        if (record.source == image_path && record.type == "texture")
            texture = id;
    check(bool(texture), "Lightmap Texture identity missing");
    auto shader = MaterialGraphSource::create(AssetId::generate());
    shader.document["graph"] = lightmap_test::graph(0);
    shader.validate();
    atomic_write(root / shader_path, shader.document.dump(2));
    catalog.add({shader.asset(), "shader", shader_path, 1, {}});
    auto material = MaterialSource::create(AssetId::generate());
    material.document["version"] = 2;
    material.document["overrides"]["model"] = surface_material_model;
    material.document["overrides"]["shader"] = shader.asset();
    const auto definition = compile_material_graph(shader.document.at("graph")).surface;
    material.document["overrides"]["textures"]["lightmap"] = {
        {"asset", texture},
        {"slot", material_values_document(surface_material_defaults(definition))
                     .at("textures")
                     .at("lightmap")}};
    material.validate();
    atomic_write(root / material_path, material.document.dump(2));
    catalog.add({material.asset(), "material", material_path, 2, {}});
    catalog.save(AssetCatalog::project_index(root));
    WorldContext world;
    Scene scene(world);
    const auto create = [&](const char* name, Double3 at) {
        const auto id =
            authoring_command(
                scene, "entity.create",
                {{"name", name}, {"position", {{"x", at[0]}, {"y", at[1]}, {"z", at[2]}}}})
                .at("selected")
                .get<std::string>();
        const auto entity = scene.entity(id);
        entity.set<Primitive>({no_primitive});
        return entity;
    };
    for (unsigned i = 0; i < 3; ++i) {
        auto entity = create(i == 0   ? "Gain 0 - no baked contribution"
                             : i == 1 ? "Gain 1 - imported irradiance"
                                      : "Gain 2 - stronger imported irradiance",
                             {double(i) * 2.5 - 2.5, 0, 0});
        MeshRenderer renderer;
        renderer.mesh = {engine_primitive(0)};
        renderer.materials = {{"surface", {material.asset()}}};
        renderer.material_parameters = {
            {"surface",
             "lightmap_gain",
             {MaterialParameterType::LinearColor3, {float(i), float(i), float(i), 0}}}};
        entity.set<MeshRenderer>(renderer);
    }
    Camera camera;
    camera.projection = unsigned(CameraProjection::Orthographic);
    camera.orthographic_height = 5;
    camera.aspect = 16.0 / 9.0;
    create("Lightmap comparison Camera", {0, 0, -7}).set<Camera>(camera);
    authoring_command(scene, "scene.rendering.set", {{"environment", {{"intensity", 0}}}});
    scene.save(root / scene_path);
    catalog = AssetCatalog::open_project(root);
    catalog.add_scene(scene_path);
    catalog.save(AssetCatalog::project_index(root));
    check(catalog.records().contains(scene.asset_id()), "Lightmap Scene was not registered");
    check(catalog.records().at(shader.asset()).metadata.empty(),
          "Source-only lightmap Shader was mislabeled as cooked");
    std::cout << "Lightmap Gallery source authored; Windows cook remains required\n";
}
void inspect(const std::filesystem::path& root) {
    const auto shader =
        MaterialGraphSource::parse(asset_detail::read_bytes(root / shader_path, 1024 * 1024));
    const auto compiled = compile_material_graph(shader.document.at("graph"));
    check(compiled.surface.uv_sets == std::vector<unsigned>{0} &&
              compiled.surface.textures.at("lightmap").semantic == TextureSemantic::HdrColor,
          "Gallery lightmap graph lost its HDR/UV interface");
    const auto material = MaterialSource::parse(
        asset_detail::read_bytes(root / material_path, material_source_byte_limit));
    const auto& overrides = material.document.at("overrides");
    check(overrides.at("shader").get<AssetId>() == shader.asset(),
          "Gallery Material references another Shader");
    const auto catalog = AssetCatalog::open_project(root);
    const auto texture_id = overrides.at("textures").at("lightmap").at("asset").get<AssetId>();
    const auto texture = asset_detail::load_texture_selection(root, catalog, {texture_id},
                                                              TextureSemantic::HdrColor);
    check(texture.value->width == 4 && texture.value->height == 4 && texture.value->mips == 3,
          "Gallery HDR lightmap lost its original dimensions or mip chain");
    WorldContext world;
    Scene scene(world);
    scene.load(root / scene_path);
    const auto rendering = extract_render_scene(scene.effective_document());
    check(rendering.meshes.size() == 3 && rendering.cameras.size() == 1 &&
              rendering.lights.empty() && rendering.diagnostics.empty(),
          "Gallery lightmap Scene differs from its three-cube unlit-environment comparison");
    for (const auto& mesh : rendering.meshes) {
        check(mesh.renderer.materials.size() == 1 &&
                  mesh.renderer.materials.front().material.id == material.asset() &&
                  mesh.renderer.material_parameters.size() == 1,
              "Gallery cube lost shared Material identity or object-local gain");
    }
    std::cout << "Lightmap Gallery source IDs, HDR Texture, graph and Scene admitted\n";
}
void prepare(const std::filesystem::path& root, const std::filesystem::path& worker) {
#ifdef FORGE_LIGHTMAP_COOK
    check(std::filesystem::exists(root / scene_path), "Lightmap Gallery source missing");
    auto lease = std::make_shared<ProjectLease>(root);
    auto shader_worker = worker.parent_path() / "forge_shader_build.exe";
    auto registry = std::make_shared<AssetImporterRegistry>();
    registry->add(asset_detail::shader_importer(shader_worker,
                                                {asset_detail::diligent_shader_compiler_digest(),
                                                 asset_detail::diligent_shader_compiler_debug()}));
    registry->seal();
    AssetImportService shaders(lease, registry, {"windows-x64", "d3d12", "fxc-5.1"});
    publish(shaders, shader_path, prepare_shader_publication);
    AssetImportService materials(lease, material_import_registry(),
                                 {"windows", "d3d12", "desktop"});
    publish(materials, material_path, prepare_material_publication);
    WorldContext world;
    Scene scene(world);
    scene.load(root / scene_path);
    const std::array roots{scene.asset_id()};
    prepare_runtime_content_catalog(*lease, roots, {}, scene.schema());
    const auto catalog = AssetCatalog::open_project(root);
    const auto shader_id = read(root / shader_path).at("asset_id").get<AssetId>();
    check(
        catalog.records().at(shader_id).metadata.at("forge.shader").at("surface_wrapper_version") ==
            current_surface_wrapper_version,
        "Packaged lightmap Gallery lacks the matching surface profile");
    std::cout << "Lightmap Gallery Shader/Material cooked with the matching Windows build\n";
#else
    (void)root;
    (void)worker;
    throw std::runtime_error("Lightmap Gallery cooking requires the Windows Shader build profile");
#endif
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 4, "Need --author/--prepare, Gallery project and matching worker");
        const auto root = std::filesystem::absolute(argv[2]);
        const auto worker = std::filesystem::absolute(argv[3]);
        if (std::string_view(argv[1]) == "--author")
            author(root, worker);
        else if (std::string_view(argv[1]) == "--inspect")
            inspect(root);
        else {
            check(std::string_view(argv[1]) == "--prepare", "Unknown Lightmap Gallery action");
            prepare(root, worker);
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
