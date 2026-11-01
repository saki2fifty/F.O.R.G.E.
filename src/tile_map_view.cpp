#include "tile_map_view.hpp"
#include "tile_map_view_impl.hpp"
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
void visit_visible_tile_chunks(const TileMapData& map, const AffineTransform& world,
                               std::span<const CameraView> cameras,
                               const std::function<void(const TileMapChunkView&)>& visit) {
    tile_detail::visit_visible(map, world, cameras, visit);
}
void visit_visible_tile_chunk_info(const TileMapData& map, const AffineTransform& world,
                                   std::span<const CameraView> cameras,
                                   const std::function<void(const TileMapChunkBounds&)>& visit) {
    map.validate();
    if (!visit)
        throw std::invalid_argument("Tile Map visible chunk-info visitor is empty");
    map.for_each_chunk_info([&](auto key, auto occupied, const void*) {
        if (const auto bounds = tile_detail::visible_chunk_bounds(key, occupied, map.layers(),
                                                                  map.cell_size(), world, cameras))
            visit({key, *bounds, occupied});
    });
}
void visit_visible_tile_chunk_values(const TileMapData& map, const AffineTransform& world,
                                     std::span<const CameraView> cameras,
                                     const std::function<void(const TileMapChunkView&)>& visit) {
    if (!visit)
        throw std::invalid_argument("Tile Map visible scoped chunk visitor is empty");
    visit_visible_tile_chunk_info(map, world, cameras, [&](const auto& info) {
        map.visit_chunk_values(
            info.key, [&](auto cells, auto) { visit({info.key, cells, info.world_bounds}); });
    });
}
} // namespace forge
