#pragma once
#include "surface_material_fixture.hpp"
#include "tile_asset_fixture.hpp"
#include "tile_asset_resources.hpp"
#include "tile_set_source.hpp"

namespace forge::test_sprite_surface_tiles {
inline void exercise(const std::filesystem::path& root, const std::filesystem::path& worker) {
    using namespace std::chrono_literals;
    using test_tile_asset_io::check;
    // CPU transport/admission only. Marker Shader bytes never reach a GPU.
    std::filesystem::create_directories(root / "Assets");
    auto lease = std::make_shared<ProjectLease>(root);
    AssetCatalog catalog(root);
    SurfaceShaderDefinition definition;
    definition.sprite_image = "image";
    definition.uv_sets = {0};
    definition.textures["image"].semantic = TextureSemantic::Color;
    definition.textures["detail"].semantic = TextureSemantic::Color;
    const auto shader = AssetId::generate();
    const auto original_shader = test::publish_surface_fixture(root, catalog, shader, definition);
    AssetImportService material_imports(lease, material_import_registry(),
                                        {"windows", "d3d12", "desktop"});
    const auto publish_material = [&](const char* path, AssetId id) {
        material_imports.submit(
            material_imports.prepare(path, {}, id),
            [](auto& candidate, const auto& plan, const auto&) {
                prepare_material_publication(candidate, plan);
            },
            [](const auto&, const auto& artifact) {
                (void)asset_detail::decode_material_bundle(artifact.files);
            });
        check(material_imports.wait_idle(15s), "Sprite Tile Material publication stalled");
        const auto results = material_imports.poll();
        check(results.size() == 1 && results[0].published,
              results.empty() ? "Missing Material receipt" : results[0].diagnostic);
    };
    const auto material_source = [&](AssetId id) {
        auto source = MaterialSource::create(id);
        source.document["version"] = 2;
        source.document["overrides"]["shader"] = shader;
        for (const auto& [key, slot] : definition.textures) {
            (void)slot;
            source.document["overrides"]["textures"][key] = {
                {"asset",
                 engine_texture(key == "image" ? EngineTexture::Checker : EngineTexture::White)},
                {"slot", material_values_document(surface_material_defaults(definition))
                             .at("textures")
                             .at(key)}};
        }
        source.validate();
        return source;
    };
    const auto good = material_source(AssetId::generate());
    atomic_write(root / "Assets/TileImage.material.json", good.document.dump(2));
    publish_material("Assets/TileImage.material.json", good.asset());
    auto source = TileSetSource::create(AssetId::generate());
    source.document["next_id"] = 3;
    source.document["images"] = {{{"id", 1},
                                  {"name", "Sprite primary image"},
                                  {"material", good.asset()},
                                  {"grid",
                                   {{"columns", 2},
                                    {"rows", 2},
                                    {"margin_x", 0},
                                    {"margin_y", 0},
                                    {"spacing_x", 0},
                                    {"spacing_y", 0}}}}};
    source.document["tiles"] = {{{"id", 2}, {"name", "Image frame"}, {"image", 1}, {"frame", 1}}};
    source.validate();
    atomic_write(root / "Assets/Sprite.tileset.json", source.document.dump(2));
    AssetImportService tiles(lease, tile_asset_import_registry(worker),
                             {"windows", "d3d12", "desktop"});
    const auto result =
        test_tile_asset_io::publish(tiles, "Assets/Sprite.tileset.json", source.asset(), false);
    check(result.published, "Tile Set rejected Sprite surface image: " + result.diagnostic);
    catalog = AssetCatalog::open_project(root);
    ResourcePool<TileSetAsset> resources;
    auto selected = std::make_shared<const AssetCatalog>(catalog);
    const auto ticket = request_tile_set(resources, root, selected, {source.asset()});
    check(resources.wait(ticket, 5s), "Sprite Tile Set resource did not load");
    const auto retained = resources.acquire(ticket);
    check(retained && retained->image(1).material.id == good.asset(),
          "Tile Set did not retain its admitted Sprite Material");
    // Checker supports a 2x2 grid; the lexically first auxiliary White is 1x1.
    // Successful cooking therefore proves the declared primary key was used.
    auto mesh_definition = definition;
    mesh_definition.sprite_image.reset();
    test::publish_surface_fixture(root, catalog, shader, mesh_definition, 2);
    const auto incompatible = material_source(AssetId::generate());
    atomic_write(root / "Assets/MeshImage.material.json", incompatible.document.dump(2));
    publish_material("Assets/MeshImage.material.json", incompatible.asset());
    catalog = AssetCatalog::open_project(root);
    const auto baseline = catalog.document();
    source.document["images"][0]["material"] = incompatible.asset();
    atomic_write(root / "Assets/Sprite.tileset.json", source.document.dump(2));
    bool rejected = false;
    try {
        const auto outcome =
            test_tile_asset_io::publish(tiles, "Assets/Sprite.tileset.json", source.asset(), false);
        rejected = !outcome.published;
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected && AssetCatalog::open_project(root).document() == baseline &&
              retained->image(1).material.id == good.asset(),
          "Mesh-only image replaced the last admitted Tile Set");
    const auto material = asset_detail::load_material_selection(root, catalog, {good.asset()});
    check(material.data.surface && material.data.surface->program.surface->sprite_image &&
              encode_shader(material.data.surface->program) == encode_shader(original_shader),
          "Current Mesh Shader publication mutated the retained Sprite Material snapshot");
}
} // namespace forge::test_sprite_surface_tiles
