#include "tile_map_view.hpp"
#include <algorithm>

namespace forge {
RenderBounds tile_map_chunk_bounds(TileChunkAddress chunk, std::array<double, 2> size,
                                   const AffineTransform& world) {
    const auto minimum = tile_cell_address({chunk, 0});
    (void)tile_cell_address({chunk, tile_chunk_cells - 1});
    for (const auto n : size)
        if (!std::isfinite(n) || n <= 0)
            throw std::invalid_argument("Tile chunk bounds need a positive finite cell size");
    // Keep full signed coordinates and the upper exclusive endpoint in doubles.
    // MeshBounds is float and cannot retain all admitted grid addresses.
    const RenderBounds local{{double(minimum.x) * size[0], double(minimum.y) * size[1], 0},
                             {(double(minimum.x) + tile_chunk_side) * size[0],
                              (double(minimum.y) + tile_chunk_side) * size[1], 0}};
    return transform_render_bounds(local, world);
}
void visit_visible_tile_chunks(const TileMapValue& map, const AffineTransform& world,
                               std::span<const CameraView> cameras,
                               const std::function<void(const TileMapChunkView&)>& visit) {
    map.validate();
    if (!visit)
        throw std::invalid_argument("Tile Map visible chunk visitor is empty");
    const auto& layers = map.layers();
    map.for_each_chunk([&](auto key, auto cells, std::size_t occupied) {
        const auto layer = std::find_if(layers.begin(), layers.end(),
                                        [&](const auto& value) { return value.id == key.layer; });
        if (!occupied || !layer->visible || cameras.empty())
            return;
        const auto bounds = tile_map_chunk_bounds(key.address, map.cell_size(), world);
        if (std::any_of(cameras.begin(), cameras.end(),
                        [&](const auto& camera) { return bounds_visible(bounds, camera); }))
            visit({key, cells, bounds});
    });
}
} // namespace forge
