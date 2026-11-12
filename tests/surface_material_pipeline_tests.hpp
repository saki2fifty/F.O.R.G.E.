#pragma once
#include "runtime_package.hpp"
#include "sprite_material_profile.hpp"
#include "surface_material_fixture.hpp"
inline void test_surface_material_pipeline(const std::filesystem::path& root, bool sprite = false,
                                           bool ui_effect = false, bool ui_textures = false) {
    using namespace forge;
    using namespace forge::asset_detail;
    using namespace std::chrono_literals;
    std::filesystem::create_directories(root / "Assets");
    AssetCatalog catalog(root);
    SurfaceShaderDefinition definition;
    definition.ui_effect = ui_effect;
    if (ui_effect)
        definition.uv_sets = {0};
    definition.parameters["tint"] = {MaterialParameterType::LinearColor4, {.2f, .4f, .8f, 1}};
    AssetId image;
    if (sprite || ui_textures) {
        if (sprite)
            definition.sprite_image = "image";
        definition.uv_sets = {0};
        definition.textures["image"].semantic = TextureSemantic::Color;
        image = texture_fixture(root, catalog, TextureDimension::D2, TextureAlpha::Straight,
                                {"windows", "d3d12", "desktop"});
    }
    const auto shader = AssetId::generate();
    const auto initial_shader = test::publish_surface_fixture(root, catalog, shader, definition);
    auto source = MaterialSource::create(AssetId::generate());
    source.document["version"] = 2;
    source.document["overrides"]["shader"] = AssetRef<ShaderAsset>{shader};
    source.document["overrides"]["parameters"]["tint"] =
        material_values_document(surface_material_defaults(definition)).at("parameters").at("tint");
    if (sprite || ui_textures)
        source.document["overrides"]["textures"]["image"] = {
            {"asset", image},
            {"slot", material_values_document(surface_material_defaults(definition))
                         .at("textures")
                         .at("image")}};
    save(root / "Assets/surface.material.json", source.document);
    catalog.add({source.asset(), "material", "Assets/surface.material.json"});
    catalog.save(AssetCatalog::project_index(root));
    auto lease = std::make_shared<ProjectLease>(root);
    AssetImportService service(lease, material_import_registry(), {"windows", "d3d12", "desktop"});
    submit(service, "Assets/surface.material.json");
    auto outcome = finish(service);
    require(outcome.published, "Surface material publication failed: " + outcome.diagnostic);
    catalog = AssetCatalog::open_project(root);
    const auto good = load_material_selection(root, catalog, {source.asset()});
    require(good.data.surface && good.data.surface->shader.id == shader &&
                good.data.surface->program.layout_digest() == initial_shader.layout_digest() &&
                good.data.values.parameters == definition.parameters,
            "Material publication lost its selected Shader interface/defaults");
    if (sprite)
        require(sprite_material_image(good.data) == "image" &&
                    good.data.textures.at("image").id == image &&
                    good.data.values.alpha == MaterialAlpha::Blend && !good.data.values.depth_write,
                "Sprite publication lost its image binding or transparent defaults");
    if (ui_effect)
        require(
            good.data.surface->program.surface->ui_effect &&
                good.data.surface->program.surface_wrapper_version == (ui_textures ? 18u : 17u) &&
                good.data.values.alpha == MaterialAlpha::Blend && good.data.values.double_sided &&
                !good.data.values.depth_test && !good.data.values.depth_write,
            "UI Material publication lost its domain, capability or native render state");
    require(catalog.dependency_graph().invalidated_by(shader) == std::vector{source.asset()},
            "Surface Shader has no material Build invalidation edge");
    auto inherited = MaterialSource::create(AssetId::generate());
    inherited.document["base"] = source.asset();
    save(root / "Assets/inherited.material.json", inherited.document);
    catalog.add({inherited.asset(), "material", "Assets/inherited.material.json"});
    catalog.save(AssetCatalog::project_index(root));
    submit(service, "Assets/inherited.material.json");
    outcome = finish(service);
    require(outcome.published, "Inherited surface failed: " + outcome.diagnostic);
    catalog = AssetCatalog::open_project(root);
    require(load_material_selection(root, catalog, {inherited.asset()}).data.surface->revision ==
                good.data.surface->revision,
            "Inherited surface did not retain the base's immutable Shader snapshot");
    auto incompatible = definition;
    incompatible.parameters.at("tint") = {MaterialParameterType::Scalar, {.5f}};
    test::publish_surface_fixture(root, catalog, shader, incompatible, 2);
    const auto before = catalog.document();
    submit(service, "Assets/surface.material.json");
    outcome = finish(service);
    require(!outcome.published && !outcome.diagnostic.empty() &&
                AssetCatalog::open_project(root).document() == before,
            "Incompatible Shader layout replaced the last-good material");
    submit(service, "Assets/inherited.material.json");
    outcome = finish(service);
    require(outcome.published, "A changed Shader poisoned an inherited last-good surface");
    // Explicit equal-value Shader selection means current selection, while
    // absence follows the base's actual immutable compiled snapshot.
    inherited.document["version"] = 2;
    inherited.document["overrides"]["shader"] = AssetRef<ShaderAsset>{shader};
    save(root / "Assets/inherited.material.json", inherited.document);
    submit(service, "Assets/inherited.material.json");
    outcome = finish(service);
    require(!outcome.published, "Explicit Shader selection bypassed changed layout validation");
    std::filesystem::remove_all(root / ".forge/cache/derived" / good.data.surface->revision);
    catalog = AssetCatalog::open_project(root);
    const auto reopened = load_material_selection(root, catalog, {source.asset()});
    require(reopened.revision == good.revision &&
                encode_shader(reopened.data.surface->program) == encode_shader(initial_shader),
            "Reload required the pruned previous Shader artifact instead of the material snapshot");
    ResourcePool<MaterialAsset> resources;
    auto ticket = request_root_material(
        resources, root, std::make_shared<const AssetCatalog>(catalog), {source.asset()});
    require(resources.wait(ticket, 5s) &&
                resources.acquire(ticket)->surface->revision == good.data.surface->revision,
            "Fresh material resource did not preserve the previous usable program");
    const auto package = root.parent_path() / ("surface-package-" + AssetId::generate().str());
    const std::array roots{source.asset()};
    package_runtime_content(root, package, roots, {"windows", "d3d12"});
    const std::filesystem::path relocated = package.string() + "-relocated";
    std::filesystem::rename(package, relocated);
    auto shipped = open_runtime_content(relocated, {"windows", "d3d12"});
    require(!shipped.records().contains(shader) &&
                shipped.records().size() == (sprite || ui_textures ? 2u : 1u) &&
                encode_shader(load_material_selection(relocated, shipped, {source.asset()})
                                  .data.surface->program) == encode_shader(initial_shader),
            "Cooked material package required current Shader/source or changed retained bytecode");
    if (sprite) {
        const auto packaged = load_material_selection(relocated, shipped, {source.asset()});
        require(sprite_material_image(packaged.data) == "image" &&
                    shipped.records().contains(image) &&
                    !std::filesystem::exists(relocated / "Assets/surface.material.json") &&
                    !std::filesystem::exists(relocated / "Assets/surface.shader.json"),
                "Source-free Sprite Material package lost its image or retained authored sources");
    }
    if (ui_textures) {
        const auto selected = load_material_selection(relocated, shipped, {source.asset()});
        validate_ui_effect_material(selected.data);
        require(shipped.records().contains(image) &&
                    selected.data.textures.at("image").id == image &&
                    !std::filesystem::exists(relocated / "Assets/surface.material.json") &&
                    !std::filesystem::exists(relocated / "Assets/surface.shader.json"),
                "Source-free UI Material lost its exact Texture closure or retained sources");
        ResourcePool<TextureAsset> textures;
        const auto texture =
            request_texture(textures, relocated, std::make_shared<const AssetCatalog>(shipped),
                            {image}, TextureSemantic::Color);
        require(textures.wait(texture, 5s) && textures.acquire(texture)->width == 1,
                "Relocated UI Material Texture required authored image data");
    }
    const auto compatible = test::publish_surface_fixture(root, catalog, shader, definition, 3);
    submit(service, "Assets/surface.material.json");
    outcome = finish(service);
    require(outcome.published, "Compatible Shader rebuild did not recover: " + outcome.diagnostic);
    catalog = AssetCatalog::open_project(root);
    const auto replacement = load_material_selection(root, catalog, {source.asset()});
    require(replacement.revision != good.revision &&
                replacement.data.surface->program.build_key == compatible.build_key,
            "Compatible Shader change did not adopt a new complete material revision");
    require(resources.acquire(ticket)->surface->program.build_key == initial_shader.build_key,
            "New publication mutated an owned prior material lease");
    auto corrupted = encode_material_bundle(source.asset(), replacement.data);
    corrupted.pop_back();
    bool rejected = false;
    try {
        (void)decode_material_bundle(corrupted, source.asset());
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "Material with missing compiled surface file was admitted");
    std::stop_source cancelled;
    cancelled.request_stop();
    rejected = false;
    try {
        (void)prepare_material_source(root, catalog, source, cancelled.get_token());
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "Cancelled Shader dependency preparation succeeded");
}
