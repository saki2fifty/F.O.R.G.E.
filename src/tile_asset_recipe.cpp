#include "tile_asset_types.hpp"
#include <forge/asset_build.hpp>

namespace forge {
std::string tile_set_recipe_revision() {
    return asset_build_digest(
        {{"recipe", "forge.tileset.v1"}, {"source_toolchain", FORGE_TILE_RECIPE_FINGERPRINT}});
}
std::string tile_map_recipe_revision() {
    return asset_build_digest(
        {{"recipe", "forge.tilemap.v1"}, {"source_toolchain", FORGE_TILE_RECIPE_FINGERPRINT}});
}
} // namespace forge
