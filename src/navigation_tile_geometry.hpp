#pragma once
#include <cstdint>
#include <forge/transform.hpp>
#include <vector>
namespace forge {
class TileMapData;
class TileSetData;
class TileMapVariants;
namespace navigation_detail {
using RegionContours = std::vector<std::vector<Double3>>;
RegionContours tile_collider_outlines(const TileMapData&, const TileSetData&,
                                      const TileMapVariants*, std::uint64_t layer,
                                      const AffineTransform&, bool obstacle);
} // namespace navigation_detail
} // namespace forge
