#pragma once
#include "model_selection.hpp"
#include <forge/animation_components.hpp>
#include <forge/model_asset.hpp>
#include <forge/scene.hpp>
#include <optional>
namespace forge::asset_detail {
struct ModelPlacementOptions {
    // Missing selection uses the declared default, then the sole scene. Multiple
    // scenes without a default require an explicit choice.
    std::optional<std::uint32_t> source_scene;
    std::string name = "Model";
    LocalTransform transform;
    // Explicit opt-in. glTF defines no default/autoplay clip; null places the
    // model with source-node TRS and morph defaults, including a static skin.
    AssetRef<AnimationClipAsset> clip;
    AssetRef<MaterialVariantAsset> material_variant;
};
// Detached preparation. No live world, files or publication are changed. The
// target scene and selected model generations are checked again at commit.
struct ModelPlacementCandidate {
    AssetId scene, model;
    std::uint64_t scene_revision = 0, model_generation = 0;
    std::string model_revision;
    EntityId root;
    Json entities;
};
ModelPlacementCandidate prepare_model_placement(const ModelSelection&, AssetId target_scene,
                                                std::uint64_t target_revision,
                                                const ModelPlacementOptions& = {});
// Shared owner-thread scene command. One validated Scene::edit / undo step;
// import publication and scene history remain separate owners.
EntityId instantiate_model(Scene&, const AssetCatalog&, const ModelPlacementCandidate&);
// Immediate owner-thread placement of one typed mesh or a linked prefab. The
// detached candidate commits once; only translation is overridden on prefabs.
EntityId instantiate_mesh(Scene&, const AssetCatalog&, AssetRef<MeshAsset>, LocalTranslation,
                          const std::string& name);
EntityId instantiate_prefab_at(Scene&, AssetId, LocalTranslation);
} // namespace forge::asset_detail
