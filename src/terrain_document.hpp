#pragma once
#include "asset_source_document.hpp"
#include <forge/terrain_source.hpp>
namespace forge {
struct TerrainDocumentTraits {
    using Source = TerrainSource;
    static constexpr auto byte_limit = terrain_source_byte_limit;
    static constexpr auto asset_type = MeshAsset::type;
    static constexpr auto suffix = ".terrain.json";
    static Source create(AssetId id) { return Source::create(id); }
};
using TerrainDocument = AssetSourceDocument<TerrainDocumentTraits>;
} // namespace forge
