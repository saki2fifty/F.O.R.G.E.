#include "tile_map_projection.hpp"
#include "cooked_envelope.hpp"
#include <algorithm>
#include <optional>
#include <set>

namespace forge {
namespace {
using Json = nlohmann::json;
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
std::uint64_t natural(const Json& value, std::uint64_t maximum) {
    require(value.is_number_unsigned() ||
                (value.is_number_integer() && value.get<std::int64_t>() >= 0),
            "Tile presentation requires a nonnegative integer");
    const auto result = value.get<std::uint64_t>();
    require(result <= maximum, "Tile presentation integer exceeds its profile");
    return result;
}
std::int32_t coordinate(const Json& value) {
    require(value.is_number_integer(), "Tile presentation coordinate must be an integer");
    if (value.is_number_unsigned()) {
        require(value.get<std::uint64_t>() <= INT32_MAX,
                "Tile presentation coordinate exceeds int32");
        return std::int32_t(value.get<std::uint64_t>());
    }
    const auto result = value.get<std::int64_t>();
    require(result >= INT32_MIN && result <= INT32_MAX,
            "Tile presentation coordinate exceeds int32");
    return std::int32_t(result);
}
struct Chunk {
    const TileMapCell* cells;
    std::size_t occupied;
};
auto chunks(const TileMapData& map) {
    std::map<TileMapChunkKey, Chunk> result;
    map.for_each_chunk(
        [&](auto key, auto cells, auto count) { result.emplace(key, Chunk{cells.data(), count}); });
    return result;
}
Json map_metadata(const TileMapData& map) {
    Json out{{"asset_id", map.asset()},          {"tile_set", map.tile_set()},
             {"cell_size", map.cell_size()},     {"layers", Json::array()},
             {"chunk_count", map.chunk_count()}, {"cell_count", map.occupied()}};
    for (const auto& layer : map.layers())
        out["layers"].push_back({{"id", layer.id},
                                 {"name", layer.name},
                                 {"visible", layer.visible},
                                 {"order", layer.order}});
    return out;
}
Json set_metadata(const TileSetData& set) {
    Json out{{"asset_id", set.asset()}, {"images", Json::array()}, {"tiles", Json::array()}};
    for (const auto& image : set.images()) {
        const auto& grid = image.grid;
        out["images"].push_back({{"id", image.id},
                                 {"name", image.name},
                                 {"material", image.material},
                                 {"grid",
                                  {{"columns", grid.columns},
                                   {"rows", grid.rows},
                                   {"margin_x", grid.margin_x},
                                   {"margin_y", grid.margin_y},
                                   {"spacing_x", grid.spacing_x},
                                   {"spacing_y", grid.spacing_y}}}});
    }
    for (const auto& tile : set.tiles())
        out["tiles"].push_back(
            {{"id", tile.id}, {"name", tile.name}, {"image", tile.image}, {"frame", tile.frame}});
    return out;
}
} // namespace
Json tile_map_projection_header(const TileMapData& map, const TileSetData& definitions) {
    require(map.tile_set() == definitions.asset(),
            "Tile presentation definitions have a different root");
    return {{"map", map_metadata(map)}, {"definitions", set_metadata(definitions)}};
}
TileMapProjectionHeader decode_tile_map_projection_header(const Json& header, AssetId map,
                                                          AssetId definitions) {
    require(header.is_object() && header.size() == 2, "Invalid Tile presentation header");
    auto metadata = header.at("map");
    TileMapProjectionHeader result;
    result.expected_cells = natural(metadata.at("cell_count"), tile_map_cell_limit);
    result.expected_chunks = natural(metadata.at("chunk_count"), tile_map_chunk_limit);
    require(result.expected_cells >= result.expected_chunks &&
                result.expected_cells <= result.expected_chunks * tile_chunk_cells,
            "Tile presentation cell/chunk counts disagree");
    metadata["cell_count"] = 0;
    metadata["chunk_count"] = 0;
    result.empty =
        decode_cooked_tile_map(asset_detail::encode_envelope(metadata, {}, tile_map_cooked_magic,
                                                             tile_map_cooked_metadata_limit),
                               map);
    result.definitions = decode_cooked_tile_set(
        asset_detail::encode_envelope(header.at("definitions"), {}, tile_set_cooked_magic,
                                      tile_set_cooked_metadata_limit),
        definitions);
    require(result.empty.tile_set() == result.definitions.asset(),
            "Tile presentation header roots disagree");
    return result;
}
std::vector<TileMapChunkKey> tile_map_projection_changes(const TileMapData& current,
                                                         const TileMapData* previous) {
    if (previous)
        require(current.asset() == previous->asset() &&
                    current.tile_set() == previous->tile_set() &&
                    current.cell_size() == previous->cell_size() &&
                    current.layers() == previous->layers(),
                "Tile presentation delta has a different identity or layout");
    const auto now = chunks(current), old = previous ? chunks(*previous) : decltype(now){};
    struct Change {
        TileMapChunkKey key;
        std::int64_t delta;
    };
    std::vector<Change> changes;
    changes.reserve(now.size() + old.size());
    for (const auto& [key, row] : old) {
        const auto at = now.find(key);
        if (at == now.end())
            changes.push_back({key, -std::int64_t(row.occupied)});
        else if (at->second.cells != row.cells)
            changes.push_back(
                {key, std::int64_t(at->second.occupied) - std::int64_t(row.occupied)});
    }
    for (const auto& [key, row] : now)
        if (!old.contains(key))
            changes.push_back({key, std::int64_t(row.occupied)});
    std::sort(changes.begin(), changes.end(), [](const auto& a, const auto& b) {
        return a.delta != b.delta ? a.delta < b.delta : a.key < b.key;
    });
    std::vector<TileMapChunkKey> result;
    result.reserve(changes.size());
    for (const auto& change : changes)
        result.push_back(change.key);
    return result;
}
Json tile_map_projection_page(const TileMapData& current, std::span<const TileMapChunkKey> keys) {
    require(keys.size() <= tile_projection_page_chunks,
            "Tile presentation page has too many chunks");
    const auto values = chunks(current);
    std::set<TileMapChunkKey> seen;
    Json page = Json::array();
    for (const auto key : keys) {
        require(seen.insert(key).second, "Tile presentation page repeats a chunk");
        require(std::any_of(current.layers().begin(), current.layers().end(),
                            [&](const auto& layer) { return layer.id == key.layer; }),
                "Tile presentation chunk has no layer");
        (void)tile_cell_address({key.address, 0});
        (void)tile_cell_address({key.address, tile_chunk_cells - 1});
        Json cells = Json::array();
        const auto found = values.find(key);
        if (found != values.end())
            for (unsigned slot = 0; slot < tile_chunk_cells; ++slot) {
                const auto cell = found->second.cells[slot];
                if (cell.tile)
                    cells.push_back({slot, cell.tile, cell.flags});
            }
        page.push_back({{"layer", key.layer},
                        {"x", key.address.x},
                        {"y", key.address.y},
                        {"cells", std::move(cells)}});
    }
    require(page.dump().size() <= tile_projection_page_bytes,
            "Tile presentation page exceeds its byte profile");
    return page;
}
TileMapData apply_tile_map_projection_page(const TileMapData& baseline,
                                           const TileSetData& definitions, const Json& page) {
    require(page.is_array() && page.size() <= tile_projection_page_chunks &&
                page.dump().size() <= tile_projection_page_bytes,
            "Invalid Tile presentation page size");
    std::set<TileMapChunkKey> seen;
    std::vector<TileMapCellEdit> writes;
    writes.reserve(page.size() * tile_chunk_cells);
    for (const auto& row : page) {
        require(row.is_object() && row.size() == 4, "Invalid Tile presentation chunk");
        const TileMapChunkKey key{natural(row.at("layer"), UINT64_MAX),
                                  {coordinate(row.at("x")), coordinate(row.at("y"))}};
        require(seen.insert(key).second, "Tile presentation page repeats a chunk");
        const auto& slots = row.at("cells");
        require(slots.is_array() && slots.size() <= tile_chunk_cells,
                "Invalid Tile presentation cell list");
        std::array<TileMapCell, tile_chunk_cells> cells{};
        std::optional<std::uint32_t> previous;
        for (const auto& tuple : slots) {
            require(tuple.is_array() && tuple.size() == 3, "Invalid Tile presentation cell tuple");
            const auto index = std::uint32_t(natural(tuple[0], tile_chunk_cells - 1));
            require(!previous || index > *previous,
                    "Tile presentation slots are repeated or unsorted");
            previous = index;
            cells[index] = {natural(tuple[1], UINT64_MAX), std::uint32_t(natural(tuple[2], 7))};
            require(cells[index].tile, "Tile presentation occupied tuple has no tile");
        }
        for (unsigned slot = 0; slot < tile_chunk_cells; ++slot)
            writes.push_back({key.layer, tile_cell_address({key.address, slot}), cells[slot]});
    }
    return baseline.with_cells(writes, definitions);
}
} // namespace forge
