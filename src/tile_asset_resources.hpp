#pragma once
#include "tile_asset_types.hpp"
#include "tile_map_archive.hpp"
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
// Private resource profiles of the existing Map root. These are not catalog
// asset types or persistent chunk IDs; variant keys distinguish partial data.
struct TileMapArchiveResource {
    static constexpr const char* type = TileMapAsset::type;
};
struct TileMapChunkResource {
    static constexpr const char* type = TileMapAsset::type;
};
template <> struct ResourceTraits<TileMapArchiveResource> {
    using Data = TileMapArchive;
};
template <> struct ResourceTraits<TileMapChunkResource> {
    using Data = TileMapChunkData;
};
// One immutable admission boundary for ordinary loading and source-free packaging.
TileSetData admit_tile_set_artifact(const CachedArtifact&, const AssetRecord&);
TileMapData admit_tile_map_artifact(const CachedArtifact&, const AssetRecord&);
TileMapArchive admit_tile_map_archive(const CachedArtifact&, const AssetRecord&);
ResourceTicket request_tile_set(ResourcePool<TileSetAsset>&, const std::filesystem::path&,
                                std::shared_ptr<const AssetCatalog>, AssetRef<TileSetAsset>);
ResourceTicket request_tile_map(ResourcePool<TileMapAsset>&, const std::filesystem::path&,
                                std::shared_ptr<const AssetCatalog>, AssetRef<TileMapAsset>);
// Complete logical values with archive-backed chunks and COW dense edits.
// Callers use the explicit scoped-value or metadata readers for these leases.
ResourceTicket request_tile_map_compact(ResourcePool<TileMapAsset>&, const std::filesystem::path&,
                                        std::shared_ptr<const AssetCatalog>,
                                        AssetRef<TileMapAsset>);
ResourceTicket request_tile_map_archive(ResourcePool<TileMapArchiveResource>&,
                                        const std::filesystem::path&,
                                        std::shared_ptr<const AssetCatalog>,
                                        AssetRef<TileMapAsset>);
// Copies a cheap immutable archive value on the caller's owner thread. The worker
// receives no lease/pointer that source-pool close could revoke. Preparation input
// is bounded by the archive byte limit and this pool's outstanding-request limit.
ResourceTicket request_tile_map_chunk(ResourcePool<TileMapChunkResource>&,
                                      const ResourceLease<TileMapArchiveResource>&,
                                      TileMapChunkKey);
// Content-addressed immutable cells also cover independently edited instances.
// The caller bounds retained input backings before submitting shared inputs.
ResourceTicket request_tile_map_chunk_values(ResourcePool<TileMapChunkResource>&,
                                             std::shared_ptr<const TileMapChunkInput>);
} // namespace forge
