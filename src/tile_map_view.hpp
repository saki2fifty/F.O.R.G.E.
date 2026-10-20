#pragma once
#include "render_bounds.hpp"
#include "tile_map_source.hpp"

namespace forge {
// Read-only projection of the same immutable map value. No serialized map body,
// per-cell entity allocation, simulation database or renderer cache is created.
struct TileMapChunkView {
    TileMapChunkKey key;
    std::span<const TileMapCell, tile_chunk_cells> cells;
    RenderBounds world_bounds;
};
RenderBounds tile_map_chunk_bounds(TileChunkAddress, std::array<double, 2> cell_size,
                                   const AffineTransform& world);
// One callback per occupied, visible-layer chunk intersecting any supplied view.
// An empty camera list produces no callbacks. Cell spans borrow the input value.
void visit_visible_tile_chunks(const TileMapValue&, const AffineTransform&,
                               std::span<const CameraView>,
                               const std::function<void(const TileMapChunkView&)>&);
} // namespace forge
