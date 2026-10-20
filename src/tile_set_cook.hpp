#pragma once
#include "tile_set_data.hpp"
#include "tile_set_source.hpp"
namespace forge {
std::vector<std::byte> cook_tile_set(const TileSetSource&);
} // namespace forge
