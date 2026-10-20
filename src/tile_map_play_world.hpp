#pragma once
#include "tile_map_play_projection.hpp"
#include <forge/world.hpp>
namespace forge {
// Read the existing synchronized instances on their owner thread. This does not
// start loads, advance gameplay, mutate cells or retain a RuntimeWorld pointer.
std::vector<TileMapPlayView> tile_map_play_views(WorldContext&);
} // namespace forge
