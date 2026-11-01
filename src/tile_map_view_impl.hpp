#pragma once
#include "tile_map_view.hpp"
#include <algorithm>
#include <optional>

namespace forge::tile_detail {
inline std::optional<RenderBounds> visible_chunk_bounds(TileMapChunkKey key, std::size_t occupied,
                                                        std::span<const TileMapLayer> layers,
                                                        std::array<double, 2> size,
                                                        const AffineTransform& world,
                                                        std::span<const CameraView> cameras) {
    const auto layer = std::find_if(layers.begin(), layers.end(),
                                    [&](const auto& value) { return value.id == key.layer; });
    if (layer == layers.end())
        throw std::invalid_argument("Tile Map visible chunk has no matching layer");
    if (!occupied || !layer->visible || cameras.empty())
        return {};
    const auto bounds = tile_map_chunk_bounds(key.address, size, world);
    if (std::any_of(cameras.begin(), cameras.end(),
                    [&](const auto& camera) { return bounds_visible(bounds, camera); }))
        return bounds;
    return {};
}
// The two concrete source/cooked adapters share culling, without retaining or
// converting either input. This helper owns no state or resource selection.
template <class Map>
void visit_visible(const Map& map, const AffineTransform& world,
                   std::span<const CameraView> cameras,
                   const std::function<void(const TileMapChunkView&)>& visit) {
    map.validate();
    if (!visit)
        throw std::invalid_argument("Tile Map visible chunk visitor is empty");
    const auto& layers = map.layers();
    map.for_each_chunk([&](auto key, auto cells, std::size_t occupied) {
        if (const auto bounds =
                visible_chunk_bounds(key, occupied, layers, map.cell_size(), world, cameras))
            visit({key, cells, *bounds});
    });
}
} // namespace forge::tile_detail
