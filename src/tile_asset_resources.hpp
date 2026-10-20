#pragma once
#include "tile_asset_types.hpp"
#include "tile_map_data.hpp"
#include "tile_set_data.hpp"
#include <forge/assets.hpp>
#include <forge/derived_cache.hpp>
#include <forge/resource.hpp>

namespace forge {
template <> struct ResourceTraits<TileSetAsset> {
    using Data = TileSetData;
};
template <> struct ResourceTraits<TileMapAsset> {
    using Data = TileMapData;
};
// One immutable admission boundary for ordinary loading and source-free packaging.
TileSetData admit_tile_set_artifact(const CachedArtifact&, const AssetRecord&);
TileMapData admit_tile_map_artifact(const CachedArtifact&, const AssetRecord&);
ResourceTicket request_tile_set(ResourcePool<TileSetAsset>&, const std::filesystem::path&,
                                std::shared_ptr<const AssetCatalog>, AssetRef<TileSetAsset>);
ResourceTicket request_tile_map(ResourcePool<TileMapAsset>&, const std::filesystem::path&,
                                std::shared_ptr<const AssetCatalog>, AssetRef<TileMapAsset>);
} // namespace forge
