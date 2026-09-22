#pragma once
#include <forge/engine_assets.hpp>
#include <forge/material_resource.hpp>
#include <forge/mesh_resource.hpp>
#include <forge/texture_resource.hpp>
namespace forge::asset_detail {
MeshResourceData engine_mesh_resource(AssetRef<MeshAsset>);
MaterialResourceData engine_material_resource(AssetRef<MaterialAsset>);
ResourceTicket request_engine_mesh(ResourcePool<MeshAsset>&, AssetRef<MeshAsset>);
ResourceTicket request_engine_material(ResourcePool<MaterialAsset>&, AssetRef<MaterialAsset>);
TextureData engine_texture_resource(AssetRef<TextureAsset>, std::optional<TextureSemantic> = {});
ResourceTicket request_engine_texture(ResourcePool<TextureAsset>&, AssetRef<TextureAsset>,
                                      std::optional<TextureSemantic> = {});
} // namespace forge::asset_detail
