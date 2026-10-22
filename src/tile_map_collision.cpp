#include "tile_map_collision.hpp"
#include <algorithm>
#include <map>

namespace forge {
std::vector<TileMapCollisionRectangle>
tile_map_collision_rectangles(const TileMapData& map, std::uint64_t layer, std::size_t limit) {
    map.validate();
    if (!limit || limit > tile_map_collision_rectangle_limit)
        throw std::invalid_argument("Tile Map collision rectangle budget must be1..4096");
    if (!layer || std::none_of(map.layers().begin(), map.layers().end(),
                               [&](const auto& row) { return row.id == layer; }))
        throw std::invalid_argument("Tile Map collision selects an unavailable layer");
    std::vector<TileMapCollisionRectangle> result;
    map.for_each_chunk([&](auto key, auto cells, auto) {
        if (key.layer != layer)
            return;
        using Run = std::pair<std::uint32_t, std::uint32_t>;
        std::map<Run, std::size_t> previous;
        for (std::uint32_t y = 0; y < tile_chunk_side; ++y) {
            std::map<Run, std::size_t> current;
            for (std::uint32_t x = 0; x < tile_chunk_side;) {
                if (!cells[y * tile_chunk_side + x].tile) {
                    ++x;
                    continue;
                }
                const auto start = x++;
                while (x < tile_chunk_side && cells[y * tile_chunk_side + x].tile)
                    ++x;
                const Run run{start, x - start};
                const auto old = previous.find(run);
                if (old != previous.end()) {
                    ++result[old->second].height;
                    current.emplace(run, old->second);
                } else {
                    if (result.size() == limit)
                        throw std::runtime_error("Tile Map collision rectangle capacity exceeded");
                    current.emplace(run, result.size());
                    result.push_back({tile_cell_address({key.address, y * tile_chunk_side + start}),
                                      run.second, 1});
                }
            }
            previous = std::move(current);
        }
    });
    return result;
}
} // namespace forge
