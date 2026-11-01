#pragma once
#include "cooked_envelope.hpp"
#include "tile_map_data.hpp"

namespace forge::tile_detail {
inline std::uint64_t read_map_u64(std::span<const std::byte> bytes, std::size_t& at) {
    const auto low = asset_detail::cooked_read32(bytes, at);
    const auto high = asset_detail::cooked_read32(bytes, at);
    return low | (std::uint64_t(high) << 32);
}
inline void write_map_u64(std::vector<std::byte>& bytes, std::uint64_t value) {
    asset_detail::cooked_write32(bytes, std::uint32_t(value));
    asset_detail::cooked_write32(bytes, std::uint32_t(value >> 32));
}
// Canonical cooked1 occupied-cell records, without the chunk header. Source
// cooking and immutable derived-page identity share this one encoding owner.
inline void append_chunk_cells(std::vector<std::byte>& bytes,
                               std::span<const TileMapCell, tile_chunk_cells> cells) {
    for (std::uint32_t slot = 0; slot < cells.size(); ++slot)
        if (cells[slot].tile) {
            asset_detail::cooked_write32(bytes, slot);
            write_map_u64(bytes, cells[slot].tile);
            asset_detail::cooked_write32(bytes, cells[slot].flags);
        }
}
inline TileMapCell admitted_chunk_cell(std::span<const std::byte> records, std::uint32_t slot) {
    if (slot >= tile_chunk_cells || records.size() % 16)
        throw std::invalid_argument("Invalid admitted Tile Map cell records");
    std::size_t first = 0, last = records.size() / 16;
    while (first < last) {
        const auto middle = first + (last - first) / 2;
        auto at = middle * 16;
        if (asset_detail::cooked_read32(records, at) < slot)
            first = middle + 1;
        else
            last = middle;
    }
    if (first == records.size() / 16)
        return {};
    auto at = first * 16;
    if (asset_detail::cooked_read32(records, at) != slot)
        return {};
    const auto tile = read_map_u64(records, at);
    return {tile, asset_detail::cooked_read32(records, at)};
}
// Callers own complete admission of sorted nonempty slots/tile keys/flags.
// This decoder consumes immutable records, not unvalidated file or wire bytes.
inline std::array<TileMapCell, tile_chunk_cells>
decode_admitted_cells(std::span<const std::byte> records, std::size_t occupied,
                      std::stop_token stop = {}) {
    if (occupied > tile_chunk_cells || records.size() != occupied * 16)
        throw std::invalid_argument("Invalid admitted Tile Map chunk records");
    if (stop.stop_requested())
        throw std::runtime_error("Tile Map chunk decoding cancelled");
    std::array<TileMapCell, tile_chunk_cells> result{};
    std::size_t at = 0;
    for (std::size_t i = 0; i < occupied; ++i) {
        if (i % 64 == 0 && stop.stop_requested())
            throw std::runtime_error("Tile Map chunk decoding cancelled");
        const auto slot = asset_detail::cooked_read32(records, at);
        if (slot >= tile_chunk_cells)
            throw std::invalid_argument("Admitted Tile Map slot is out of range");
        const auto tile = read_map_u64(records, at);
        result[slot] = {tile, asset_detail::cooked_read32(records, at)};
    }
    if (stop.stop_requested())
        throw std::runtime_error("Tile Map chunk decoding cancelled");
    return result;
}
} // namespace forge::tile_detail
