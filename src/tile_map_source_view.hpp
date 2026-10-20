#pragma once
#include "tile_map_source.hpp"
#include "tile_map_view.hpp"

namespace forge {
// Authoring adapter is separate from the cooked-only runtime view dependency.
void visit_visible_tile_chunks(const TileMapValue&, const AffineTransform&,
                               std::span<const CameraView>,
                               const std::function<void(const TileMapChunkView&)>&);
} // namespace forge
