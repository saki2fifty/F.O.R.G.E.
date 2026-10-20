#include "tile_map_source_view.hpp"
#include "tile_map_view_impl.hpp"

namespace forge {
void visit_visible_tile_chunks(const TileMapValue& map, const AffineTransform& world,
                               std::span<const CameraView> cameras,
                               const std::function<void(const TileMapChunkView&)>& visit) {
    tile_detail::visit_visible(map, world, cameras, visit);
}
} // namespace forge
