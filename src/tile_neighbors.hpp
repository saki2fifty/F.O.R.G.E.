#pragma once
#include <array>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>

namespace forge {
inline constexpr std::size_t tile_neighbor_rule_limit = 64;
namespace tile_detail {
inline constexpr std::array<std::array<int, 2>, 8> tile_neighbor_directions{
    {{0, 1}, {1, 1}, {1, 0}, {1, -1}, {0, -1}, {-1, -1}, {-1, 0}, {-1, 1}}};
} // namespace tile_detail
struct TileNeighborRule {
    std::uint8_t mask = 0, same = 0;
    std::uint64_t tile = 0;
    bool operator==(const TileNeighborRule&) const = default;
};
using TileNeighborTable = std::array<std::uint8_t, 256>;
namespace tile_detail {
inline TileNeighborTable compile_tile_neighbors(std::span<const TileNeighborRule> rules) {
    if (rules.size() > tile_neighbor_rule_limit)
        throw std::invalid_argument("Tile neighbor rules exceed64rows");
    for (const auto& rule : rules)
        if (!rule.tile || (rule.same & ~rule.mask))
            throw std::invalid_argument("Invalid neighbor rule");
    TileNeighborTable table;
    table.fill(255);
    for (unsigned mask = 0; mask < table.size(); ++mask)
        for (std::size_t row = 0; row < rules.size(); ++row)
            if ((mask & rules[row].mask) == rules[row].same) {
                table[mask] = std::uint8_t(row);
                break;
            }
    return table;
}
inline std::uint64_t select_tile_neighbor(std::span<const TileNeighborRule> rules,
                                          const TileNeighborTable& table, std::uint8_t mask,
                                          std::uint64_t fallback) {
    return table[mask] == 255 ? fallback : rules[table[mask]].tile;
}
inline std::uint8_t tile_neighbor_local_mask(std::uint8_t mask, std::uint32_t flags) {
    if (flags & ~7u)
        throw std::invalid_argument("Unsupported cell orientation");
    static constexpr auto permutation = [] {
        std::array<std::array<std::uint8_t, 8>, 8> result{};
        for (unsigned flags = 0; flags < 8; ++flags)
            for (unsigned bit = 0; bit < 8; ++bit) {
                auto [x, y] = tile_neighbor_directions[bit];
                // Map XY/up to source XY/up, following transpose-before-UV-flips.
                if (flags & 4) {
                    const auto old_x = x;
                    x = -y;
                    y = -old_x;
                }
                if (flags & 1)
                    x = -x;
                if (flags & 2)
                    y = -y;
                for (unsigned at = 0; at < 8; ++at)
                    if (tile_neighbor_directions[at] == std::array{x, y})
                        result[flags][bit] = std::uint8_t(at);
            }
        return result;
    }();
    std::uint8_t result = 0;
    for (unsigned bit = 0; bit < 8; ++bit)
        if (mask & (1u << bit))
            result |= std::uint8_t(1u << permutation[flags][bit]);
    return result;
}
template <class Read>
std::uint8_t tile_neighbor_mask(std::int32_t x, std::int32_t y, std::uint64_t tile, Read&& read) {
    if (!tile)
        return 0;
    std::uint8_t mask = 0;
    for (unsigned bit = 0; bit < 8; ++bit) {
        const auto nx = std::int64_t(x) + tile_neighbor_directions[bit][0];
        const auto ny = std::int64_t(y) + tile_neighbor_directions[bit][1];
        if (nx >= INT32_MIN && nx <= INT32_MAX && ny >= INT32_MIN && ny <= INT32_MAX &&
            read(std::int32_t(nx), std::int32_t(ny)) == tile)
            mask |= std::uint8_t(1u << bit);
    }
    return mask;
}
} // namespace tile_detail
} // namespace forge
