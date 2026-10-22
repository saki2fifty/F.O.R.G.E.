#include "cooked_envelope.hpp"
#include "tile_map_collision.hpp"
#include <bit>
#include <iostream>
#include <random>
#include <set>

using namespace forge;
namespace {
constexpr std::uint64_t selected_layer = UINT64_MAX - 1;
using Cells = std::map<TileMapChunkKey, std::map<std::uint32_t, TileMapCell>>;
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F> void reject(F&& action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid Tile Map collision candidate was accepted");
}
TileMapData map_value(const Cells& cells) {
    const auto asset = AssetId::generate();
    std::vector<std::byte> payload;
    std::size_t total = 0;
    const auto write = [&](std::uint32_t n) { asset_detail::cooked_write32(payload, n); };
    const auto wide = [&](std::uint64_t n) {
        write(std::uint32_t(n));
        write(std::uint32_t(n >> 32));
    };
    for (const auto& [key, values] : cells) {
        wide(key.layer);
        write(std::bit_cast<std::uint32_t>(key.address.x));
        write(std::bit_cast<std::uint32_t>(key.address.y));
        write(std::uint32_t(values.size()));
        for (const auto& [index, value] : values) {
            write(index);
            wide(value.tile);
            write(value.flags);
        }
        total += values.size();
    }
    const nlohmann::json metadata{
        {"asset_id", asset},
        {"tile_set", AssetId::generate()},
        {"cell_size", {.25, 2.}},
        {"chunk_count", cells.size()},
        {"cell_count", total},
        {"layers",
         {{{"id", 1}, {"name", "Decoration"}, {"visible", true}, {"order", -4}},
          {{"id", selected_layer}, {"name", "Solid"}, {"visible", false}, {"order", 9}}}}};
    return decode_cooked_tile_map(asset_detail::encode_envelope(metadata, payload,
                                                                tile_map_cooked_magic,
                                                                tile_map_cooked_metadata_limit),
                                  asset);
}
std::set<TileCellAddress> occupied(const Cells& cells, std::uint64_t layer) {
    std::set<TileCellAddress> result;
    for (const auto& [key, values] : cells)
        if (key.layer == layer)
            for (const auto& [index, value] : values)
                if (value.tile)
                    result.insert(tile_cell_address({key.address, index}));
    return result;
}
void coverage(const Cells& cells, std::uint64_t layer = selected_layer) {
    const auto map = map_value(cells);
    const auto rectangles = tile_map_collision_rectangles(map, layer);
    require(rectangles == tile_map_collision_rectangles(map, layer),
            "Immutable collision projection is not deterministic");
    std::set<TileCellAddress> actual;
    for (const auto& rectangle : rectangles) {
        require(rectangle.width && rectangle.height && rectangle.width <= tile_chunk_side &&
                    rectangle.height <= tile_chunk_side,
                "Collision rectangle exceeded its nonempty chunk profile");
        const auto chunk = tile_chunk_cell(rectangle.origin).chunk;
        for (std::uint32_t y = 0; y < rectangle.height; ++y)
            for (std::uint32_t x = 0; x < rectangle.width; ++x) {
                const auto wide_x = std::int64_t(rectangle.origin.x) + x;
                const auto wide_y = std::int64_t(rectangle.origin.y) + y;
                require(wide_x <= INT32_MAX && wide_y <= INT32_MAX,
                        "Collision rectangle overflowed signed cell addresses");
                const TileCellAddress cell{std::int32_t(wide_x), std::int32_t(wide_y)};
                require(tile_chunk_cell(cell).chunk == chunk,
                        "Collision rectangle crossed its chunk boundary");
                require(actual.insert(cell).second, "Collision rectangles overlap");
            }
    }
    require(actual == occupied(cells, layer),
            "Collision rectangles filled an empty cell or omitted an occupied one");
    require(map.occupied() == occupied(cells, selected_layer).size() + occupied(cells, 1).size(),
            "Collision projection changed its immutable source cells");
}
void simple_and_edges() {
    coverage({});
    Cells cells;
    for (std::uint32_t y = 2; y < 7; ++y)
        for (std::uint32_t x = 3; x < 10; ++x)
            cells[{selected_layer, {-1, 0}}][y * tile_chunk_side + x] = {17, (x + y) % 8};
    // A different-width run must not merge with the five preceding rows.
    cells[{selected_layer, {-1, 0}}][7 * tile_chunk_side + 3] = {99, 0};
    // A gap must break the same-width vertical run.
    cells[{selected_layer, {-1, 0}}][9 * tile_chunk_side + 3] = {99, 7};
    cells[{1, {-1, 0}}][0] = {55, 3};
    const auto map = map_value(cells);
    const std::vector<TileMapCollisionRectangle> expected{
        {{-29, 2}, 7, 5}, {{-29, 7}, 1, 1}, {{-29, 9}, 1, 1}};
    require(tile_map_collision_rectangles(map, selected_layer) == expected,
            "Solid hidden layer lost stable wide ID, maximal row runs or gap separation");
    coverage(cells);
    coverage(cells, 1);
    for (const TileCellAddress cell : {TileCellAddress{-33, -33},
                                       {-32, -32},
                                       {-1, -1},
                                       {0, 0},
                                       {31, 31},
                                       {32, 32},
                                       {INT32_MIN, INT32_MIN},
                                       {INT32_MAX, INT32_MAX}}) {
        const auto at = tile_chunk_cell(cell);
        cells[{selected_layer, at.chunk}][at.index] = {UINT64_MAX - 1, 7};
    }
    coverage(cells);
    reject([&] { (void)tile_map_collision_rectangles(map, 0); });
    reject([&] { (void)tile_map_collision_rectangles(map, 2); });
    reject([&] { (void)tile_map_collision_rectangles(map, selected_layer, 0); });
    reject([&] { (void)tile_map_collision_rectangles(map, selected_layer, 4097); });
    reject([&] { (void)tile_map_collision_rectangles(map, selected_layer, 2); });
    reject([&] { (void)tile_map_collision_rectangles(TileMapData{}, selected_layer); });
    require(tile_map_collision_rectangles(map, selected_layer, 3) == expected,
            "Failed bounded projection changed a subsequent valid candidate");
}
void random_and_capacity() {
    std::mt19937 generator(0x4f524745);
    for (int example = 0; example < 32; ++example) {
        Cells cells;
        for (std::int32_t chunk = -2; chunk <= 2; ++chunk)
            for (std::uint32_t index = 0; index < tile_chunk_cells; ++index)
                if (generator() % 4 == 0)
                    cells[{selected_layer, {chunk, example - 16}}][index] = {
                        std::uint64_t(generator()) + 1, std::uint32_t(generator() % 8)};
        coverage(cells);
    }
    Cells dense;
    for (std::int32_t chunk = 0; chunk < 256; ++chunk)
        for (std::uint32_t index = 0; index < tile_chunk_cells; ++index)
            dense[{selected_layer, {chunk, -1}}][index] = {1, index % 8};
    const auto maximum = map_value(dense);
    require(maximum.occupied() == tile_map_cell_limit &&
                tile_map_collision_rectangles(maximum, selected_layer).size() == 256,
            "Maximum dense Map did not merge to one rectangle per full chunk");
    coverage(dense);
    Cells sparse;
    for (std::int32_t chunk = 0; chunk < 1024; ++chunk)
        sparse[{selected_layer, {chunk, -chunk}}][unsigned(chunk) % tile_chunk_cells] = {3, 7};
    require(tile_map_collision_rectangles(map_value(sparse), selected_layer).size() == 1024,
            "Maximum sparse chunk count lost collision geometry");
    coverage(sparse);
    Cells checkerboard;
    for (std::int32_t chunk = 0; chunk < 8; ++chunk)
        for (std::uint32_t y = 0; y < tile_chunk_side; ++y)
            for (std::uint32_t x = 0; x < tile_chunk_side; ++x)
                if ((x + y) % 2 == 0)
                    checkerboard[{selected_layer, {chunk, 0}}][y * tile_chunk_side + x] = {1, 0};
    require(tile_map_collision_rectangles(map_value(checkerboard), selected_layer).size() ==
                tile_map_collision_rectangle_limit,
            "Exact collision rectangle capacity did not remain available");
    coverage(checkerboard);
    checkerboard[{selected_layer, {8, 0}}][0] = {1, 0};
    const auto excessive = map_value(checkerboard);
    reject([&] { (void)tile_map_collision_rectangles(excessive, selected_layer); });
    require(excessive.occupied() == 4097,
            "Rejected excessive rectangle projection changed admitted cells");
}
} // namespace
int main() {
    try {
        simple_and_edges();
        random_and_capacity();
        std::cout << "Tile Map collision rectangle coverage/bounds/capacity PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
