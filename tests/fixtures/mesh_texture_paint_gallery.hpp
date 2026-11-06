#pragma once
#include "../render_texture_fixture.hpp"
#include "asset_storage.hpp"
#include "model_placement.hpp"
#include "model_render_resource.hpp"
#include "paint_texture_document.hpp"
#include "paint_texture_mesh.hpp"
#include "runtime_package.hpp"
#include "texture_authoring.hpp"
#include <forge/animation_components.hpp>
#include <forge/authoring.hpp>
#include <forge/model_asset.hpp>
#include <forge/render_components.hpp>
#include <fstream>

namespace forge::test {
inline constexpr auto mesh_paint_scene_path = "Scenes/178-mesh-texture-painting.scene.json";
inline constexpr auto mesh_paint_image_path = "Assets/Painting/MeshSurface.paint-texture.json";
inline constexpr auto mesh_paint_material_path = "Assets/Painting/MeshSurface.material.json";
inline AssetId author_mesh_texture_paint_gallery(const std::filesystem::path& root,
                                                 const std::filesystem::path& worker) {
    if (std::filesystem::exists(root / mesh_paint_scene_path) ||
        std::filesystem::exists(root / mesh_paint_image_path) ||
        std::filesystem::exists(root / mesh_paint_material_path))
        throw std::runtime_error("Mesh texture painting Gallery sources already exist");
    auto catalog = AssetCatalog::open_project(root);
    const auto before = catalog.document().at("assets");
    AssetId model_id;
    for (const auto& [id, record] : catalog.records())
        if (record.type == ModelAsset::type && record.source == "Assets/UI/MeshSurface/Panel.gltf")
            model_id = id;
    if (!model_id)
        throw std::runtime_error("Original animated panel publication is missing");
    const auto model = asset_detail::load_model_selection(root, catalog, model_id);
    AssetRef<MeshAsset> mesh;
    for (const auto& [key, id] : model.bindings) {
        (void)key;
        if (model.member(id).identity.type == MeshAsset::type) {
            if (mesh.id)
                throw std::runtime_error("Panel fixture has more than one Mesh");
            mesh.id = id;
        }
    }
    if (!mesh.id)
        throw std::runtime_error("Panel fixture Mesh is missing");
    const auto lease = std::make_shared<ProjectLease>(root);
    auto image = PaintTextureDocument::create(lease, mesh_paint_image_path, [](AssetId id) {
        return PaintTextureSource{
            PaintTextureValue::create(id, 128, 128, TextureSemantic::Color, {14, 26, 42, 255})};
    });
    const auto image_id = image->source().asset();
    test_render_texture::publish(lease, *image, texture_import_registry(worker),
                                 prepare_texture_publication);
    auto material = MaterialDocument::create(lease, mesh_paint_material_path, [&](AssetId id) {
        auto source = MaterialSource::create(id);
        MaterialData shape;
        shape.model = "forge.gltf.unlit.v1";
        shape.textures["baseColorTexture"].semantic = TextureSemantic::Color;
        shape.textures["baseColorTexture"].sampler.u =
            shape.textures["baseColorTexture"].sampler.v = TextureWrap::ClampEdge;
        source.document["overrides"] = {
            {"model", shape.model},
            {"double_sided", true},
            {"textures",
             {{"baseColorTexture",
               {{"asset", image_id},
                {"slot",
                 material_values_document(shape).at("textures").at("baseColorTexture")}}}}}};
        return source;
    });
    const auto material_id = material->source().asset();
    test_render_texture::publish(lease, *material, material_import_registry(),
                                 prepare_material_publication);
    catalog = AssetCatalog::open_project(root);
    PaintMeshSelection selection{mesh,
                                 {material_id},
                                 asset_detail::selected_mesh_revision(catalog, mesh),
                                 asset_detail::selected_material_revision(catalog, {material_id})};
    PaintBrush brush;
    brush.radius = 8;
    brush.hardness = .8;
    brush.color = {1, .25, .025, 1};
    const std::array<PaintMeshPoint, 3> left{
        {{0, {0, {.8, .1, .1}}}, {0, {0, {.1, .1, .8}}}, {0, {1, {.1, .1, .8}}}}};
    const auto orange =
        project_paint_mesh_stroke(root, catalog, selection, left, image->source().document);
    image->edit_source(image->revision(), "Paint orange mesh path", [&](auto& source) {
        source.document = source.document.paint(orange, brush);
    });
    brush.color = {.025, .75, 1, 1};
    const std::array<PaintMeshPoint, 3> right{
        {{1, {0, {.8, .1, .1}}}, {1, {0, {.1, .1, .8}}}, {1, {1, {.1, .1, .8}}}}};
    const auto cyan =
        project_paint_mesh_stroke(root, catalog, selection, right, image->source().document);
    image->edit_source(image->revision(), "Paint cyan mesh path",
                       [&](auto& source) { source.document = source.document.paint(cyan, brush); });
    test_render_texture::publish(lease, *image, texture_import_registry(worker),
                                 prepare_texture_publication);
    // Refresh the ordinary dependent publication for reproducible export metadata.
    test_render_texture::publish(lease, *material, material_import_registry(),
                                 prepare_material_publication);
    catalog = AssetCatalog::open_project(root);
    EngineContext engine;
    Scene scene(engine.world());
    asset_detail::ModelPlacementOptions options;
    options.name = "Painted model - expand and select Surface";
    options.transform.scale = {4, 3, 2};
    options.clip = {model.bindings.at("/animations/0")};
    (void)asset_detail::instantiate_model(
        scene, catalog,
        asset_detail::prepare_model_placement(model, scene.asset_id(), scene.revision(), options));
    unsigned mesh_count = 0;
    const auto placed = scene.document();
    for (const auto& row : placed.at("entities")) {
        auto actor = scene.entity(row.at("id").get<std::string>());
        if (!actor.has<MeshRenderer>())
            continue;
        auto renderer = actor.get<MeshRenderer>();
        renderer.materials.clear();
        const auto data = asset_detail::model_mesh_resource(model, renderer.mesh);
        for (const auto& slot : data.materials)
            renderer.materials.push_back({slot.key, {material_id}});
        actor.set<MeshRenderer>(renderer);
        ++mesh_count;
    }
    if (mesh_count != 1)
        throw std::runtime_error("Mesh painting Gallery expected one multipart Mesh");
    const auto camera =
        authoring_command(scene, "entity.create", {{"recipe", "empty"}, {"name", "Game camera"}})
            .at("selected")
            .get<std::string>();
    Camera lens;
    lens.aspect = 16. / 9;
    lens.far_plane = 100;
    scene.entity(camera).set<Camera>(lens).set<LocalTranslation>({0, 0, -6});
    scene.save(root / mesh_paint_scene_path);
    catalog = AssetCatalog::open_project(root);
    const auto record = catalog.add_scene(mesh_paint_scene_path);
    catalog.save(AssetCatalog::project_index(root));
    const std::array roots{record.id};
    prepare_runtime_content_catalog(*lease, roots, {}, scene.schema());
    const auto after = AssetCatalog::open_project(root).document().at("assets");
    if (after.size() != before.size() + 3)
        throw std::runtime_error("Mesh painting Gallery changed unrelated asset count");
    for (const auto& row : before)
        if (std::find(after.begin(), after.end(), row) == after.end())
            throw std::runtime_error("Mesh painting Gallery changed an existing record");
    return record.id;
}
} // namespace forge::test
