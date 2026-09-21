#pragma once
#include <forge/assets.hpp>
#include <forge/derived_cache.hpp>
#include <forge/material_resource.hpp>
namespace forge::asset_detail {
struct MaterialSelection {
    AssetId asset;
    std::string revision;
    std::uint64_t generation{};
    MaterialResourceData data;
};
std::vector<ArtifactFile> encode_material_bundle(AssetId, const MaterialResourceData&);
MaterialResourceData decode_material_bundle(const std::vector<ArtifactFile>&,
                                            AssetId expected = {});
MaterialSelection load_material_selection(const std::filesystem::path&, const AssetCatalog&,
                                          AssetRef<MaterialAsset>, std::stop_token = {});
ResourceTicket request_root_material(ResourcePool<MaterialAsset>&, std::filesystem::path,
                                     std::shared_ptr<const AssetCatalog>, AssetRef<MaterialAsset>);
} // namespace forge::asset_detail
