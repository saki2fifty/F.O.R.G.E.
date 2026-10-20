#pragma once
#include <forge/asset_ref.hpp>
#include <string>

namespace forge {
// Internal root tags until the complete scene/runtime SDK surface is admitted.
// Local image/tile/layer/chunk/cell keys are not catalog asset types.
struct TileSetAsset {
    static constexpr const char* type = "tile_set";
};
struct TileMapAsset {
    static constexpr const char* type = "tile_map";
};
inline constexpr const char* tile_set_importer_id = "forge.tileset";
inline constexpr const char* tile_map_importer_id = "forge.tilemap";
inline constexpr const char* tile_set_cooked_format = "forge.tile-set-cooked";
inline constexpr const char* tile_map_cooked_format = "forge.tile-map-cooked";
inline constexpr const char* tile_set_artifact_file = "tileset.bin";
inline constexpr const char* tile_map_artifact_file = "tilemap.bin";
std::string tile_set_recipe_revision();
std::string tile_map_recipe_revision();
} // namespace forge
