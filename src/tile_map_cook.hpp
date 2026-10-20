#pragma once
#include "tile_map_data.hpp"
#include "tile_map_source.hpp"

namespace forge {
std::vector<std::byte> cook_tile_map(const TileMapValue&);
} // namespace forge
