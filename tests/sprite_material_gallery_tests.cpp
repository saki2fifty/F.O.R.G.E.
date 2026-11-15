#include "asset_bytes.hpp"
#include "asset_storage.hpp"
#include "gallery_asset_publication.hpp"
#include "material_authoring.hpp"
#include "material_selection.hpp"
#include "runtime_package.hpp"
#include "shader_pipeline.hpp"
#include "sprite_material_gallery_fixture.hpp"
#include "sprite_material_profile.hpp"
#ifdef FORGE_GALLERY_SHADER_COOK
#include "shader_authoring.hpp"
#include "shader_diligent.hpp"
#endif
#include <array>
#include <forge/authoring.hpp>
#include <forge/material_graph.hpp>
#include <forge/primitive_catalog.hpp>
#include <forge/render_scene.hpp>
#include <iostream>
#include <set>

using namespace forge;
namespace {
using namespace forge::test_sprite_material_gallery;
void author(const std::filesystem::path& root) {
    check(!std::filesystem::exists(root / scene_path), "Sprite Material Gallery already exists");
    auto catalog = AssetCatalog::open_project(root);
    const auto image = asset(catalog, image_path);
    std::filesystem::create_directories(root / "Assets/Sprites/Materials/Shaders");
    const auto graph = MaterialGraphSource::create_sprite(AssetId::generate());
    SurfaceShaderDefinition raw;
    raw.sprite_image = "image";
    raw.uv_sets = {0};
    raw.textures["image"].semantic = TextureSemantic::Color;
    raw.parameters["gain"] = {MaterialParameterType::Scalar, {1}};
    raw.labels["gain"] = "Image gain";
    Json raw_source{
        {"format", "forge.shader"},
        {"version", 2},
        {"asset_id", AssetId::generate()},
        {"source_root", "Assets/Sprites/Materials/Shaders"},
        {"surface", surface_definition_document(raw)},
        {"stages",
         Json::array({{{"stage", "pixel"}, {"source", "image.hlsl"}, {"entry", "GalleryImage"}}})}};
    atomic_write(root / "Assets/Sprites/Materials/Shaders/image.hlsl",
                 "#include \"engine/forge.surface.hlsli\"\n"
                 "float4 GalleryImage(ForgeSpriteInput input) {\n"
                 "    return ForgeSample_image(input) * ForgeParameter_gain() * "
                 "input.Color;\n"
                 "}\n");
    std::array<AssetId, 2> material_ids;
    const std::array documents{raw_source, graph.document};
    for (unsigned i = 0; i < documents.size(); ++i) {
        const auto program = shader_program_source(documents[i]);
        check(program.surface && program.surface->sprite_image,
              "Sprite Shader factory lost its declared primary image");
        atomic_write(root / shaders[i], documents[i].dump(2));
        const auto shader = documents[i].at("asset_id").get<AssetId>();
        catalog.add({shader, "shader", shaders[i], 1, {}});
        auto material = MaterialSource::create(AssetId::generate());
        material_ids[i] = material.asset();
        material.document["version"] = 2;
        auto& overrides = material.document["overrides"];
        overrides["shader"] = shader;
        const auto defaults = surface_material_defaults(*program.surface);
        const auto key = *program.surface->sprite_image;
        overrides["textures"][key] = {
            {"asset", image}, {"slot", material_values_document(defaults).at("textures").at(key)}};
        for (const auto& [name, parameter] : program.surface->parameters)
            overrides["parameters"][name] = {
                {"type", unsigned(parameter.type)},
                {"value", i ? Json::array({.55, 1., .75, 1.}) : Json::array({1.})}};
        material.validate();
        atomic_write(root / materials[i], material.document.dump(2));
        catalog.add({material.asset(), "material", materials[i], 1, {}});
    }
    catalog.save(AssetCatalog::project_index(root));
    WorldContext world;
    Scene scene(world);
    const auto create = [&](const char* name, double x, double y, double z) {
        auto entity = scene.entity(
            authoring_command(scene, "entity.create",
                              {{"name", name}, {"position", {{"x", x}, {"y", y}, {"z", z}}}})
                .at("selected")
                .get<std::string>());
        entity.set<Primitive>({no_primitive});
        return entity;
    };
    constexpr std::array names{"HLSL / image gain", "Graph / material tint",
                               "HLSL / sliced with frame filtering",
                               "Graph / tiled with frame filtering"};
    for (unsigned i = 0; i < names.size(); ++i) {
        SpriteRenderer sprite;
        sprite.material = {material_ids[i % 2]};
        sprite.width = 4;
        sprite.height = 2.4;
        if (i >= 2) {
            sprite.mip_filter = unsigned(SpriteMipFilter::Linear);
            sprite.image_mode = unsigned(i == 2 ? SpriteImageMode::Sliced : SpriteImageMode::Tiled);
            sprite.pixels_per_meter = 16;
            if (i == 2)
                sprite.border_left = sprite.border_right = sprite.border_top =
                    sprite.border_bottom = 6;
        }
        create(names[i], i % 2 ? 2.6 : -2.6, i < 2 ? 1.7 : -1.7, 0).set(sprite);
    }
    Camera camera;
    camera.projection = unsigned(CameraProjection::Orthographic);
    camera.orthographic_height = 8;
    camera.aspect = 16. / 9.;
    create("Sprite Material Camera", 0, 0, -8).set(camera);
    scene.save(root / scene_path);
    catalog = AssetCatalog::open_project(root);
    catalog.add_scene(scene_path);
    catalog.save(AssetCatalog::project_index(root));
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
    AssetImportService shader_imports(lease, registry, {"windows-x64", "d3d12", "fxc-5.1"});
    for (unsigned i = 0; i < shaders.size(); ++i) {
        const auto captured = asset_detail::capture_shader_source(root, shaders[i]);
        check(captured.project_sources.size() == (i ? 0 : 1),
              "Gallery Shader source closure differs");
    }
    for (auto path : shaders)
        test::publish_gallery_asset(
            shader_imports, path, prepare_shader_publication,
            std::chrono::seconds(asset_detail::shader_worker_limits().seconds + 15));
    AssetImportService material_imports(lease, material_import_registry(),
                                        {"windows", "d3d12", "desktop"});
    for (auto path : materials)
        test::publish_gallery_asset(material_imports, path, prepare_material_publication);
    WorldContext world;
    Scene scene(world);
    scene.load(root / scene_path);
    const std::array roots{scene.asset_id()};
    prepare_runtime_content_catalog(*lease, roots, {}, scene.schema());
    const auto catalog = AssetCatalog::open_project(root);
    for (auto path : materials) {
        const auto selected =
            asset_detail::load_material_selection(root, catalog, {asset(catalog, path)});
        check(selected.data.surface &&
                  selected.data.surface->program.surface_wrapper_version ==
                      current_surface_wrapper_version &&
                  !sprite_material_image(selected.data).empty(),
              "Gallery Material lost its matching cooked Sprite program");
    }
#else
    (void)root;
    (void)worker;
    throw std::runtime_error("Sprite Material Gallery cooking requires the Windows Shader profile");
#endif
}
void verify_export(const std::filesystem::path& root, const std::filesystem::path& worker) {
    prepare(root, worker);
    const auto catalog = AssetCatalog::open_project(root);
    const auto scene = asset(catalog, scene_path);
    const std::set expected{scene, asset(catalog, image_path), asset(catalog, materials[0]),
                            asset(catalog, materials[1])};
    const std::array roots{scene};
    const auto packaged = root.parent_path() / "sprite-content";
    const auto relocated = root.parent_path() / "relocated-sprite-content";
    (void)package_runtime_content(root, packaged, roots, {"windows", "d3d12"});
    const auto offline = root.parent_path() / "source-offline";
    std::filesystem::rename(root, offline);
    std::filesystem::rename(packaged, relocated);
    const auto cooked = open_runtime_content(relocated, {"windows", "d3d12"});
    std::set<AssetId> actual;
    for (const auto& [id, record] : cooked.records()) {
        actual.insert(id);
        check(record.source != catalog.records().at(id).source,
              "Sprite Material export retained an authored source locator");
    }
    check(actual == expected, "Sprite Material export contains the wrong runtime closure");
    check(!std::filesystem::exists(relocated / "Assets") &&
              !std::filesystem::exists(relocated / "Scenes"),
          "Sprite Material export retained authored source directories");
    for (auto path : materials) {
        const auto original =
            asset_detail::load_material_selection(offline, catalog, {asset(catalog, path)});
        const auto loaded =
            asset_detail::load_material_selection(relocated, cooked, {asset(catalog, path)});
        check(original.data.surface && loaded.data.surface &&
                  encode_shader(original.data.surface->program) ==
                      encode_shader(loaded.data.surface->program) &&
                  sprite_material_image(original.data) == sprite_material_image(loaded.data),
              "Relocation changed the embedded Sprite program or primary image binding");
    }
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 4, "Need action, Gallery project and matching worker");
        const auto root = std::filesystem::absolute(argv[2]);
        const auto action = std::string_view(argv[1]);
        if (action == "--author")
            author(root);
        else if (action == "--inspect")
            inspect(root);
        else if (action == "--verify-export")
            verify_export(root, std::filesystem::absolute(argv[3]));
        else {
            check(action == "--prepare", "Unknown Sprite Material Gallery action");
            prepare(root, std::filesystem::absolute(argv[3]));
        }
        std::cout << "Sprite Material Gallery " << action << " completed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
