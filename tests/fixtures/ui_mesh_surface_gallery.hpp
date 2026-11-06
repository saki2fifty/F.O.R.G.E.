#pragma once
#include "asset_import_service.hpp"
#include "asset_storage.hpp"
#include "model_authoring.hpp"
#include "model_importer.hpp"
#include "model_placement.hpp"
#include "model_render_resource.hpp"
#include "runtime_package.hpp"
#include <chrono>
#include <forge/authoring.hpp>
#include <forge/project_lease.hpp>
#include <forge/render_components.hpp>
#include <forge/ui_assets.hpp>
#include <forge/ui_components.hpp>
#include <fstream>

namespace forge::test {
inline AssetId author_ui_mesh_surface_gallery(const std::filesystem::path& root,
                                              const RuntimeUiInspector& inspect,
                                              const std::filesystem::path& worker,
                                              const std::filesystem::path& converter) {
    using namespace asset_detail;
    using namespace std::chrono_literals;
    const std::filesystem::path scene_path = "Scenes/177-animated-mesh-ui.scene.json";
    const std::filesystem::path model_path = "Assets/UI/MeshSurface/Panel.gltf";
    if (std::filesystem::exists(root / scene_path))
        throw std::runtime_error("Animated mesh UI Gallery already exists");
    auto registry = std::make_shared<AssetImporterRegistry>();
    registry->add(model_importer(worker, converter));
    registry->seal();
    const auto lease = std::make_shared<ProjectLease>(root);
    AssetImportService imports(lease, registry, {"windows", "d3d12", "desktop"}, 1);
    const auto draft = imports.prepare(model_path);
    imports.submit(
        draft,
        [](auto& candidate, const auto& plan, const auto& catalog) {
            prepare_model_publication(candidate, plan, catalog);
        },
        [](const auto&, const auto&) {});
    if (!imports.wait_idle(60s))
        throw std::runtime_error("Animated mesh UI import timed out");
    const auto receipts = imports.poll();
    if (receipts.size() != 1 || !receipts[0].published)
        throw std::runtime_error(receipts.empty() ? "Animated mesh UI import receipt missing"
                                                  : receipts[0].diagnostic);
    auto catalog = AssetCatalog::open_project(root);
    AssetId model;
    for (const auto& [id, record] : catalog.records())
        if (record.source == model_path && record.type == ModelAsset::type)
            model = id;
    if (!model)
        throw std::runtime_error("Animated mesh UI model is not published");
    const auto selected = load_model_selection(root, catalog, model);
    Json source;
    std::ifstream(root / "Scenes/174-interactive-ui-surfaces.scene.json") >> source;
    AssetRef<RenderTextureAsset> output;
    AssetRef<MaterialAsset> material;
    Json entities = Json::array();
    for (auto row : source.at("entities")) {
        auto& values = row.at("components");
        if (values.contains("forge.ui_surface_input")) {
            output.id = values.at("forge.ui_surface_input").at("render_texture").get<AssetId>();
            material.id = values.at("forge.mesh_renderer")
                              .at("materials")
                              .at(0)
                              .at("material")
                              .get<AssetId>();
            continue;
        }
        row["id"] = EntityId::generate();
        entities.push_back(std::move(row));
    }
    if (!output.id || !material.id)
        throw std::runtime_error("Animated mesh UI source surface assignment missing");
    EngineContext engine;
    Scene scene(engine.world());
    source["asset_id"] = scene.asset_id();
    source["entities"] = std::move(entities);
    scene.restore_snapshot(source);
    ModelPlacementOptions options;
    options.name = "Animated bent UI panel - two parts, skin and morph";
    options.transform.translation = {-2.2, 0, 0};
    options.transform.scale = {3, 1.5, 1};
    options.clip = {selected.bindings.at("/animations/0")};
    (void)instantiate_model(
        scene, catalog,
        prepare_model_placement(selected, scene.asset_id(), scene.revision(), options));
    const auto placed = scene.document();
    unsigned surfaces = 0;
    for (const auto& row : placed.at("entities")) {
        if (!row.at("components").contains("forge.model_source") ||
            !row.at("components").contains("forge.mesh_renderer"))
            continue;
        auto actor = scene.entity(row.at("id").get<std::string>());
        auto renderer = actor.get<MeshRenderer>();
        const auto data = model_mesh_resource(selected, renderer.mesh);
        renderer.materials.clear();
        for (const auto& slot : data.materials)
            renderer.materials.push_back({slot.key, material});
        renderer.layers = 4;
        renderer.cast_shadows = false;
        actor.set<MeshRenderer>(renderer);
        actor.set<UiSurfaceInput>({output, true, std::uint32_t(UiSurfaceGeometry::Mesh)});
        ++surfaces;
    }
    if (surfaces != 1)
        throw std::runtime_error("Animated mesh UI expected one placed multi-part mesh");
    asset_storage::replace(root / "Assets/UI/MeshSurface/Guide.rml", R"rml(<rml><head><style>
body,div {display:block;pointer-events:none;font-family:Lato;color:#e7f4ff;}
body {margin:0;width:100%;height:100%;}
#title {position:absolute;left:3%;top:5%;font-size:24px;color:#67f1b9;}
#guide {position:absolute;left:3%;bottom:4%;width:94%;padding:8px;box-sizing:border-box;font-size:16px;background-color:#102438e8;}
</style></head><body><div id="title">UI on an animated mesh</div><div id="guide">LEFT: two-part bent mesh with a moving support joint and Bend morph.<br/>Click Pause, Step, Resume or the text field. Input follows the rendered pose and UV0.<br/>RIGHT: display-only relay. Capture Game input first.</div></body></rml>)rml");
    const auto guide = register_ui_document(root, "Assets/UI/MeshSurface/Guide.rml");
    for (const auto& row : placed.at("entities")) {
        auto actor = scene.entity(row.at("id").get<std::string>());
        if (actor.has<UiDocument>() && !actor.get<UiDocument>().render_texture.id)
            actor.set<UiDocument>({{guide.id}, true, true, 0});
    }
    scene.save(root / scene_path);
    catalog = AssetCatalog::open_project(root);
    const auto record = catalog.add_scene(scene_path);
    catalog.save(AssetCatalog::project_index(root));
    const std::array roots{record.id};
    prepare_runtime_content_catalog(*lease, roots, inspect, scene.schema());
    return record.id;
}
} // namespace forge::test
