#pragma once
#include "sprite_draw_values.hpp"
#include "tile_grid.hpp"
#include "tile_set_data.hpp"
#include <forge/transform.hpp>

namespace forge {
// Derived draw values only. The caller retains the admitted Set/image selection;
// no source document, cell identity, GPU object or resource owner is created here.
struct TileDrawValues {
    std::uint64_t image = 0;
    AssetRef<MaterialAsset> material;
    SpriteDrawValues sprite;
};
inline TileDrawValues tile_draw_values(const TileSetData& definitions, std::uint64_t tile,
                                       std::uint32_t flags, std::uint32_t texture_width,
                                       std::uint32_t texture_height,
                                       std::array<float, 4> color = {1, 1, 1, 1}) {
    if (flags > 7)
        throw std::invalid_argument("Unsupported Tile Map UV transform flags");
    const auto& entry = definitions.tile(tile);
    const auto& image = definitions.image(entry.image);
    const auto region = sprite_grid_region(texture_width, texture_height, image.grid, entry.frame);
    SpriteDrawValues values{sprite_region_mapping(region, texture_width, texture_height,
                                                  bool(flags & 1), bool(flags & 2)),
                            color};
    values.transpose_uv = flags & 4;
    validate_sprite_draw_values(values);
    return {image.id, image.material, values};
}
// The shared Quad is centered. Cells cover their ordinary local XY grid area;
// preserve double coordinates until the existing camera-relative GPU upload.
inline AffineTransform tile_cell_world(TileCellAddress cell, std::array<double, 2> size,
                                       const AffineTransform& world) {
    for (auto value : size)
        if (!std::isfinite(value) || value <= 0)
            throw std::invalid_argument("Tile draw cell size must be positive and finite");
    const AffineTransform local{{size[0], 0, 0, (double(cell.x) + .5) * size[0], 0, size[1], 0,
                                 (double(cell.y) + .5) * size[1], 0, 0, 1, 0}};
    const auto result = world * local;
    for (auto value : result.m)
        if (!std::isfinite(value))
            throw std::invalid_argument("Tile draw world transform is not finite");
    return result;
}
} // namespace forge
