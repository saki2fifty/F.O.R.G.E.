#pragma once
#include "sprite_region.hpp"
#include <forge/asset_ref.hpp>
#include <string>
#include <vector>

namespace forge {
inline constexpr std::size_t tile_set_image_limit = 16, tile_set_definition_limit = 4096;
struct TileSetImage {
    std::uint64_t id = 0;
    std::string name;
    AssetRef<MaterialAsset> material;
    SpriteGridLayout grid;
};
struct TileSetDefinition {
    std::uint64_t id = 0, image = 0;
    std::string name;
    std::uint32_t frame = 0;
};
struct TileSetRows {
    std::vector<TileSetImage> images;
    std::vector<TileSetDefinition> tiles;
};
namespace tile_detail {
// Shared value admission. Source owns unknown fields/allocation history;
// the cooked decoder separately requires exactly its runtime fields.
TileSetRows decode_tile_set_rows(const nlohmann::json& images, const nlohmann::json& tiles);
} // namespace tile_detail
} // namespace forge
