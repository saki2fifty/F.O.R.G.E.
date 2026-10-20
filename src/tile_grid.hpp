#pragma once
#include <array>
#include <cmath>
#include <compare>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace forge {
// Private Tile Map addressing. Cells cover [x,x+1) x [y,y+1) in local XY;
// Y increases upward. Chunking is derived storage, never persistent identity.
inline constexpr std::int32_t tile_chunk_side = 32;
inline constexpr std::uint32_t tile_chunk_cells = tile_chunk_side * tile_chunk_side;
struct TileCellAddress {
    std::int32_t x = 0, y = 0;
    auto operator<=>(const TileCellAddress&) const = default;
};
struct TileChunkAddress {
    std::int32_t x = 0, y = 0;
    auto operator<=>(const TileChunkAddress&) const = default;
};
struct TileChunkCell {
    TileChunkAddress chunk;
    std::uint32_t index = 0; // Row-major, with row zero at the smallest local Y.
    bool operator==(const TileChunkCell&) const = default;
};
inline TileChunkCell tile_chunk_cell(TileCellAddress cell) {
    const auto split = [](std::int32_t n) {
        auto quotient = n / tile_chunk_side, remainder = n % tile_chunk_side;
        if (remainder < 0) {
            --quotient;
            remainder += tile_chunk_side;
        }
        return std::array{quotient, remainder};
    };
    const auto x = split(cell.x), y = split(cell.y);
    return {{x[0], y[0]}, std::uint32_t(y[1] * tile_chunk_side + x[1])};
}
inline TileCellAddress tile_cell_address(TileChunkCell address) {
    if (address.index >= tile_chunk_cells)
        throw std::invalid_argument("Tile chunk cell index is outside its grid");
    const auto axis = [](std::int32_t chunk, std::uint32_t offset) {
        const auto n = std::int64_t(chunk) * tile_chunk_side + offset;
        if (n < INT32_MIN || n > INT32_MAX)
            throw std::invalid_argument(
                "Tile chunk is outside signed cell coordinates: chunk=" + std::to_string(chunk) +
                ", offset=" + std::to_string(offset) + ", cell=" + std::to_string(n));
        return std::int32_t(n);
    };
    return {axis(address.chunk.x, address.index % tile_chunk_side),
            axis(address.chunk.y, address.index / tile_chunk_side)};
}
inline TileCellAddress tile_cell_at(std::array<double, 2> local, std::array<double, 2> cell_size) {
    const auto axis = [](double position, double size) {
        if (!std::isfinite(position) || !std::isfinite(size) || size <= 0)
            throw std::invalid_argument(
                "Tile local position/size must be finite and size positive");
        const auto cell = std::floor(position / size);
        if (!std::isfinite(cell) || cell < INT32_MIN || cell > INT32_MAX)
            throw std::invalid_argument("Tile local position is outside signed cell coordinates");
        return std::int32_t(cell);
    };
    return {axis(local[0], cell_size[0]), axis(local[1], cell_size[1])};
}
} // namespace forge
