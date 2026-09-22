#pragma once
#include "gltf_variant_fixture.hpp"
#include <forge/authoring.hpp>
#include <forge/prefab_authoring.hpp>
template <class Run>
void check_material_variant_pipeline(const std::filesystem::path& root,
                                     forge::AssetImportService& service, Run&& run) {
    using namespace forge;
    using namespace forge::asset_detail;
    auto input = gltf_variant_fixture();
    const auto path = std::filesystem::path("Assets/material-variants.gltf");
    atomic_write(root / path, input.document.dump());
    write(root / "Assets/instances.bin", input.buffers[0].bytes());
    const auto imported = run(path);
    require(imported.published, imported.diagnostic.c_str());
    const auto owner = service.prepare(path).request.asset;
    const auto selected = load_model_selection(root, imported.publication->catalog, owner);
    const AssetRef<MeshAsset> mesh{selected.bindings.at("/lods/0")};
    const AssetRef<MaterialVariantAsset> split{selected.bindings.at("/material_variants/0")};
    const AssetRef<MaterialVariantAsset> partial{selected.bindings.at("/material_variants/1")};
    const AssetRef<MaterialAsset> base{selected.bindings.at("/materials/0")};
    const AssetRef<MaterialAsset> low{selected.bindings.at("/materials/1")};
    const AssetRef<MaterialAsset> alternative{selected.bindings.at("/materials/2")};
    const auto data = model_mesh_resource(selected, mesh);
    require(data.variants.size() == 2 && data.mesh.lods.size() == 2,
            "Variant/LOD resource mapping was discarded");
    const auto normal = select_mesh_materials(data, {});
    require(normal.parts[0] == std::vector{base, base} && normal.parts[1] == std::vector{low},
            "No variant must use original materials");
    const auto changed = select_mesh_materials(data, {}, split);
    require(changed.parts[0] == std::vector{low, alternative} &&
                changed.parts[1] == std::vector{alternative},
            "Variant collapsed shared-base primitives or lost lower LOD selection");
    const auto fallback = select_mesh_materials(data, {}, partial);
    require(fallback.parts[0] == std::vector{low, base} && fallback.parts[1] == std::vector{low},
            "Unmapped variant primitives did not fall back to base materials");
    const std::array overrides{MaterialSlotOverride{"material:" + base.id.str(), {}}};
    const auto explicit_default = select_mesh_materials(data, overrides, split);
    require(!explicit_default.parts[0][0].id && !explicit_default.parts[0][1].id &&
                explicit_default.parts[1][0] == alternative,
            "Explicit null slot overrides must win over variant selection");
    rejects([&] { select_mesh_materials(data, {}, {AssetId::generate()}); });
    auto corrupt = data;
    corrupt.variants[0].mappings[{0, 99}] = alternative;
    rejects([&] { validate_mesh_material_bindings(corrupt); });
    ResourcePool<MeshAsset> meshes;
    ResourcePool<MaterialAsset> materials;
    ResourcePool<TextureAsset> textures;
    const auto catalog = std::make_shared<const AssetCatalog>(imported.publication->catalog);
    auto prepare = [&](AssetRef<MaterialVariantAsset> variant) {
        ModelDrawCandidate draw(root, catalog, 1, mesh, {}, meshes, {}, variant);
        for (unsigned attempt = 0; attempt < 3000 && draw.state() == ResourceState::Loading;
             ++attempt) {
            meshes.pump();
            materials.pump();
            textures.pump();
            draw.advance(1, meshes, materials, textures);
            if (draw.state() == ResourceState::Loading)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        require(draw.ready(), draw.diagnostic().c_str());
        return *draw.ready();
    };
    const auto first_draw = prepare(split), other_draw = prepare(partial);
    require(first_draw.mesh.identity() == other_draw.mesh.identity() &&
                first_draw.materials.contains(alternative.id) &&
                !other_draw.materials.contains(alternative.id) &&
                prepared_model_draw_key(first_draw, false) !=
                    prepared_model_draw_key(other_draw, false),
            "Variant switching duplicated geometry, loaded unused materials or aliased draw "
            "candidates");
    EngineContext engine;
    Scene scene(engine.world());
    scene.reset(empty_scene());
    ModelPlacementOptions options;
    options.material_variant = partial;
    const auto placed_root = instantiate_model(
        scene, *catalog,
        prepare_model_placement(selected, scene.asset_id(), scene.revision(), options));
    const auto original = scene.document();
    const auto count = scene.entity_count();
    for (const auto& row : original.at("entities"))
        if (row.at("components").contains("forge.mesh_renderer"))
            require(scene.entity(row.at("id").get<std::string>())
                            .get<MeshRenderer>()
                            .material_variant == partial,
                    "Placement lost the selected material variant");
    authoring_command(scene, "model.material_variant",
                      {{"entity", placed_root}, {"variant", split.id}});
    const auto edited = scene.document();
    require(scene.entity_count() == count, "Variant switching duplicated model nodes");
    for (const auto& row : edited.at("entities"))
        if (row.at("components").contains("forge.mesh_renderer"))
            require(row.at("components")
                            .at("forge.mesh_renderer")
                            .at("material_variant")
                            .template get<AssetId>() == split.id,
                    "Whole-model operation omitted a mesh node");
    scene.undo();
    require(scene.document() == original, "Variant Undo lost scene values");
    scene.redo();
    require(scene.document() == edited, "Variant Redo lost scene values");
    const auto prefab = create_prefab_source(scene, placed_root.str());
    scene.publish_prefab_sources({{prefab.asset(), prefab.source}}, [] {});
    const auto instance = instantiate_prefab(scene, prefab.asset());
    const auto before_prefab = scene.document();
    authoring_command(scene, "model.material_variant",
                      {{"entity", instance}, {"variant", split.id}});
    bool intent = false;
    const auto prefab_edited = scene.document();
    for (const auto& row : prefab_edited.at("entities"))
        if (row.contains("property_overrides") &&
            row.at("property_overrides").contains("forge.mesh_renderer")) {
            const auto& fields = row.at("property_overrides").at("forge.mesh_renderer");
            require(fields.size() == 1 &&
                        fields.at("material_variant").template get<AssetId>() == split.id,
                    "Variant selection made unrelated prefab overrides");
            intent = true;
        }
    require(intent, "Equal-value prefab material variant intent was lost");
    scene.undo();
    require(scene.document() == before_prefab, "Variant Undo failed for prefab instance");
    auto copy = edited;
    // Older in-phase MeshRenderer data omitted this optional field.
    for (auto& row : copy["entities"])
        if (row["components"].contains("forge.mesh_renderer"))
            row["components"]["forge.mesh_renderer"].erase("material_variant");
    EngineContext legacy_engine;
    Scene old(legacy_engine.world());
    old.reset(copy);
    const auto old_document = old.document();
    for (const auto& row : old_document.at("entities"))
        if (row.at("components").contains("forge.mesh_renderer"))
            require(row.at("components").at("forge.mesh_renderer").at("material_variant").is_null(),
                    "Legacy MeshRenderer did not default to base materials");
    auto reordered = input.document;
    auto& variants = reordered["extensions"]["KHR_materials_variants"]["variants"];
    std::reverse(variants.begin(), variants.end());
    variants[1]["name"] = "Renamed split surfaces";
    for (auto& m : reordered["meshes"])
        for (auto& p : m["primitives"])
            for (auto& mapping : p["extensions"]["KHR_materials_variants"]["mappings"])
                for (auto& v : mapping["variants"])
                    v = 1u - v.get<unsigned>();
    atomic_write(root / path, reordered.dump());
    const auto reimport = run(path);
    require(reimport.published, reimport.diagnostic.c_str());
    const auto next = load_model_selection(root, reimport.publication->catalog, owner);
    require(next.bindings.at("/material_variants/1") == split.id &&
                next.bindings.at("/material_variants/0") == partial.id,
            "Variant reordering/renaming changed persistent identities");
    require(select_mesh_materials(model_mesh_resource(next, mesh), {}, split).parts ==
                changed.parts,
            "Stable variant selection changed after source reorder");
    auto invalid = reordered;
    invalid["meshes"][0]["primitives"][0]["extensions"]["KHR_materials_variants"]["mappings"][0]
           ["material"] = 999;
    atomic_write(root / path, invalid.dump());
    const auto failed = run(path);
    require(!failed.published, "Invalid variant replacement was published");
    require(load_model_selection(root, AssetCatalog::open_project(root), owner).revision ==
                    next.revision &&
                first_draw.selection.parts == changed.parts,
            "Variant failure replaced the selected family or previous resource lease");
    auto ambiguous = reordered;
    auto& ambiguous_variants = ambiguous["extensions"]["KHR_materials_variants"]["variants"];
    ambiguous_variants = Json::array({{{"name", "Duplicate"}}, {{"name", "Duplicate"}}});
    for (auto& m : ambiguous["meshes"])
        for (auto& p : m["primitives"])
            for (auto& mapping : p["extensions"]["KHR_materials_variants"]["mappings"])
                mapping["variants"] = {0, 1};
    atomic_write(root / path, ambiguous.dump());
    const auto conflict = run(path);
    require(!conflict.published && !conflict.identity_conflicts.empty(),
            "Ambiguous material variants guessed persistent correspondence");
    auto removed = reordered;
    removed["extensions"]["KHR_materials_variants"]["variants"].erase(0);
    for (auto& m : removed["meshes"])
        for (auto& p : m["primitives"]) {
            auto& mappings = p["extensions"]["KHR_materials_variants"]["mappings"];
            for (auto it = mappings.begin(); it != mappings.end();) {
                const auto& source_variants = it->at("variants");
                if (std::find(source_variants.begin(), source_variants.end(), Json(1)) ==
                    source_variants.end())
                    it = mappings.erase(it);
                else {
                    (*it)["variants"] = {0};
                    ++it;
                }
            }
            if (mappings.empty())
                p["extensions"].erase("KHR_materials_variants");
        }
    atomic_write(root / path, removed.dump());
    const auto removal = run(path);
    require(removal.published &&
                removal.publication->catalog.records().at(partial.id).subasset->removed,
            "Removed variant lost its identity tombstone");
    const auto remaining = load_model_selection(root, removal.publication->catalog, owner);
    const auto remaining_mesh = model_mesh_resource(remaining, mesh);
    rejects([&] { select_mesh_materials(remaining_mesh, {}, partial); });
    require(select_mesh_materials(remaining_mesh, {}, split).parts == changed.parts,
            "Removing another variant changed a surviving selection");
    const auto packaged = root.parent_path() / ("variant-package-" + AssetId::generate().str());
    const std::array package_roots{split.id};
    const RuntimePackageTarget target{"portable", "none"};
    (void)package_runtime_content(root, packaged, package_roots, target);
    const auto packaged_catalog = open_runtime_content(packaged, target);
    const auto packaged_model = load_model_selection(packaged, packaged_catalog, owner);
    require(select_mesh_materials(model_mesh_resource(packaged_model, mesh), {}, split).parts ==
                    changed.parts &&
                !std::filesystem::exists(packaged / path),
            "Source-free variant package lost selected material mappings");
    std::filesystem::remove_all(packaged);
}
