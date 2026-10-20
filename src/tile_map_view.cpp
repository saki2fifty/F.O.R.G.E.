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
} // namespace forge
