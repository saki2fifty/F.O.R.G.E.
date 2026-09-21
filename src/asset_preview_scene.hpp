#pragma once
#include "model_placement.hpp"
#include "spatial_document.hpp"
#include <algorithm>
#include <forge/render_scene.hpp>
namespace forge::asset_detail {
struct ModelPreviewScene {
    RenderScene scene;
    unsigned source_scene = 0, source_scene_count = 0;
    std::size_t nodes = 0;
};
// Detached read-only projection through the existing placement and transform
// contracts. No live world, project mutation, persistent identity or second model
// hierarchy is created. Generated entity IDs belong only to this copied view.
inline ModelPreviewScene prepare_model_preview(const ModelSelection& selected,
                                               std::optional<unsigned> source_scene = {},
                                               AssetRef<MeshAsset> only_mesh = {}) {
    const auto& hierarchy = selected.index.hierarchy;
    ModelPreviewScene result;
    result.source_scene_count = unsigned(hierarchy.at("scenes").size());
    if (source_scene)
        result.source_scene = *source_scene;
    else if (!hierarchy.at("default_scene").is_null())
        result.source_scene = hierarchy.at("default_scene").get<unsigned>();
    ModelPlacementOptions options;
    if (result.source_scene_count)
        options.source_scene = result.source_scene;
    else if (source_scene && *source_scene != 0)
        throw std::runtime_error("Model has no numbered source scenes");
    if (only_mesh.id && selected.member(only_mesh.id).identity.type != MeshAsset::type)
        throw std::runtime_error("Mesh preview selection has the wrong asset type");
    const auto id = AssetId::generate();
    auto placed = prepare_model_placement(selected, id, 0, options);
    result.nodes = placed.entities.size();
    Json snapshot{{"asset_id", id}, {"entities", std::move(placed.entities)}};
    snapshot = detail::project_spatial(std::move(snapshot));
    result.scene = extract_render_scene(snapshot);
    if (!result.scene.diagnostics.empty())
        throw std::runtime_error(result.scene.diagnostics.front().text);
    if (only_mesh.id) {
        std::erase_if(result.scene.meshes,
                      [&](const auto& mesh) { return mesh.renderer.mesh != only_mesh; });
        if (result.scene.meshes.empty())
            throw std::runtime_error("This mesh has no placement in the selected source scene. "
                                     "Choose another source scene or inspect the owning model.");
        // Keep copied model node/joint scope: filtering the visible mesh must
        // not turn a skinned instance into an unrelated bind-pose approximation.
    }
    result.scene.settings.shadows.enabled = false;
    for (auto& mesh : result.scene.meshes) {
        mesh.renderer.cast_shadows = false;
        mesh.renderer.receive_shadows = false;
    }
    return result;
}
} // namespace forge::asset_detail
