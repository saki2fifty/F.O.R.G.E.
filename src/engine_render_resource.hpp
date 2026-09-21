#pragma once
#include <forge/engine_assets.hpp>
#include <forge/material_resource.hpp>
#include <forge/mesh_resource.hpp>
namespace forge::asset_detail {
MeshResourceData engine_mesh_resource(AssetRef<MeshAsset>);
MaterialResourceData engine_material_resource(AssetRef<MaterialAsset>);
ResourceTicket request_engine_mesh(ResourcePool<MeshAsset>&, AssetRef<MeshAsset>);
ResourceTicket request_engine_material(ResourcePool<MaterialAsset>&, AssetRef<MaterialAsset>);
} // namespace forge::asset_detail
