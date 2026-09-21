#pragma once
#include "model_selection.hpp"
#include <forge/material_resource.hpp>
#include <forge/mesh_resource.hpp>
#include <forge/texture_resource.hpp>
namespace forge::asset_detail {
// Prepared from one fully admitted immutable family, with no source parser or
// graphics device. Physical material ordinals never escape as persistent keys.
MeshResourceData model_mesh_resource(const ModelSelection&, AssetRef<MeshAsset>);
MaterialResourceData model_material_resource(const ModelSelection&, AssetRef<MaterialAsset>,
                                             const MaterialLayout&);
MaterialResourceData model_pbr_material_resource(const ModelSelection&, AssetRef<MaterialAsset>);
TextureData model_texture_resource(const ModelSelection&, AssetRef<TextureAsset>, TextureSemantic);
// Omitted semantic selects HDR color when available, otherwise color; never data/normal.
// Root texture assets and model members share the same async identity/lifetime path.
ResourceTicket request_texture(ResourcePool<TextureAsset>&, std::filesystem::path project,
                               std::shared_ptr<const AssetCatalog>, AssetRef<TextureAsset>,
                               std::optional<TextureSemantic> = {});
// Request metadata comes from a copied catalog selection; worker preparation
// verifies that exact family before the existing pool's owner-thread adoption.
ResourceTicket request_model_mesh(ResourcePool<MeshAsset>&, std::filesystem::path project,
                                  std::shared_ptr<const AssetCatalog>, AssetRef<MeshAsset>);
ResourceTicket request_model_material(ResourcePool<MaterialAsset>&, std::filesystem::path project,
                                      std::shared_ptr<const AssetCatalog>, AssetRef<MaterialAsset>,
                                      MaterialLayout);
} // namespace forge::asset_detail

namespace forge::asset_detail {
// Built-in model declarations own compatibility; callers need not parse an asset
// on the presentation thread to discover its reflected parameter layout.
ResourceTicket request_model_pbr_material(ResourcePool<MaterialAsset>&,
                                          std::filesystem::path project,
                                          std::shared_ptr<const AssetCatalog>,
                                          AssetRef<MaterialAsset>);
} // namespace forge::asset_detail
