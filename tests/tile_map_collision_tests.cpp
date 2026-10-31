#include "cooked_envelope.hpp"
#include "tile_draw_values.hpp"
#include "tile_map_collision.hpp"
#include "tile_set_cook.hpp"
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
TileMapData map_value(const Cells& cells, AssetId definitions = {}) {
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
        {"tile_set", definitions ? definitions : AssetId::generate()},
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
void definition_shapes() {
    auto source = TileSetSource::create(AssetId::generate());
    source.document["next_id"] = 5;
    source.document["images"].push_back({{"id", 1},
                                         {"name", "Collision image"},
                                         {"material", AssetId::generate()},
                                         {"grid",
                                          {{"columns", 1},
                                           {"rows", 1},
                                           {"margin_x", 0},
                                           {"margin_y", 0},
                                           {"spacing_x", 0},
                                           {"spacing_y", 0}}}});
    for (std::uint64_t id = 2; id <= 4; ++id)
        source.document["tiles"].push_back(
            {{"id", id}, {"name", "Tile"}, {"image", 1}, {"frame", 0}});
    source.set_collision(3, {{"kind", "none"}});
    source.set_collision(4, {{"kind", "convex"}, {"points", {{0., 0.}, {1., 0.}, {.75, 1.}}}});
    const auto definitions = decode_cooked_tile_set(cook_tile_set(source), source.asset());
    for (std::uint32_t flags = 0; flags <= 7; ++flags) {
        const Cells cells{
            {{selected_layer, {0, 0}}, {{0, {2, 0}}, {1, {3, 0}}, {2, {2, 7}}, {3, {4, flags}}}}};
        const auto map = map_value(cells, source.asset());
        const auto geometry = tile_map_collision_geometry(map, definitions, selected_layer, 3);
        require(geometry == tile_map_collision_geometry(map, definitions, selected_layer),
                "Tile definition geometry is not deterministic");
        const std::vector<TileMapCollisionRectangle> boxes{{{0, 0}, 1, 1}, {{2, 0}, 1, 1}};
        require(geometry.rectangles == boxes && geometry.polygons.size() == 1 &&
                    geometry.polygons[0].origin == TileCellAddress{3, 0} &&
                    geometry.polygons[0].count == 3,
                "No-collision/custom definitions did not split full-cell runs");
        const auto draw = tile_draw_values(definitions, 4, flags, 16, 16);
        for (std::size_t i = 0; i < 3; ++i) {
            const auto& point = geometry.polygons[0].points[i];
            std::array<double, 2> uv{point[0], 1 - point[1]};
            if (draw.sprite.transpose_uv)
                std::swap(uv[0], uv[1]);
            for (unsigned axis = 0; axis < 2; ++axis)
                uv[axis] =
                    draw.sprite.region.origin[axis] + uv[axis] * draw.sprite.region.scale[axis];
            const auto& original = definitions.tile(4).collision.points[i];
            require(std::abs(uv[0] - original[0]) < 1e-12 &&
                        std::abs(uv[1] - (1 - original[1])) < 1e-12,
                    "Collision transform differs from the actual Sprite atlas/transpose mapping");
        }
        reject([&] { (void)tile_map_collision_geometry(map, definitions, selected_layer, 2); });
    }
    auto concave_source = source;
    concave_source.set_collision(
        4, {{"kind", "polygon"},
            {"points", {{0., 0.}, {1., 0.}, {1., .5}, {.5, .5}, {.5, 1.}, {0., 1.}}}});
    const auto concave = decode_cooked_tile_set(cook_tile_set(concave_source), source.asset(), 3);
    for (std::uint32_t flags = 0; flags < 8; ++flags) {
        const Cells cells{{{selected_layer, {0, 0}}, {{0, {2, 0}}, {1, {3, 0}}, {2, {4, flags}}}}};
        const auto map = map_value(cells, source.asset());
        const auto parts = tile_map_collision_geometry(map, concave, selected_layer, 5);
        require(parts.rectangles.size() == 1 && parts.polygons.size() == 4,
                "Concave tile did not retain its four pieces beside FullCell/None");
        const auto draw = tile_draw_values(concave, 4, flags, 16, 16);
        for (unsigned y = 0; y < 8; ++y)
            for (unsigned x = 0; x < 8; ++x) {
                const std::array<double, 2> point{(x + .5) / 8, (y + .5) / 8};
                auto uv = std::array{point[0], 1 - point[1]};
                if (draw.sprite.transpose_uv)
                    std::swap(uv[0], uv[1]);
                for (unsigned axis = 0; axis < 2; ++axis)
                    uv[axis] =
                        draw.sprite.region.origin[axis] + uv[axis] * draw.sprite.region.scale[axis];
                const bool filled = uv[0] < .5 || (1 - uv[1]) < .5;
                bool hit = false;
                for (const auto& piece : parts.polygons) {
                    require(piece.count == 3 && piece.origin == TileCellAddress{2, 0},
                            "Concave piece escaped its existing cell address");
                    const auto& a = piece.points[0];
                    const auto& b = piece.points[1];
                    const auto& c = piece.points[2];
                    const double denominator =
                        (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1]);
                    const double u =
                        ((b[1] - c[1]) * (point[0] - c[0]) + (c[0] - b[0]) * (point[1] - c[1])) /
                        denominator;
                    const double v =
                        ((c[1] - a[1]) * (point[0] - c[0]) + (a[0] - c[0]) * (point[1] - c[1])) /
                        denominator;
                    hit |= u >= -1e-12 && v >= -1e-12 && u + v <= 1 + 1e-12;
                }
                require(hit == filled,
                        "Concave pieces disagree with actual flipped/transposed artwork");
            }
        reject([&] { (void)tile_map_collision_geometry(map, concave, selected_layer, 4); });
    }
    Cells filled;
    for (std::uint32_t slot = 0; slot < 1024; ++slot)
        filled[{selected_layer, {0, 0}}][slot] = {4, slot % 8};
    require(tile_map_collision_geometry(map_value(filled, source.asset()), concave, selected_layer)
                    .polygons.size() == 4096,
            "Maximum decomposed convex-piece budget rejected");
    filled[{selected_layer, {1, 0}}][0] = {4, 0};
    reject([&] {
        (void)tile_map_collision_geometry(map_value(filled, source.asset()), concave,
                                          selected_layer);
    });
    const auto convex_envelope =
        asset_detail::decode_envelope(cook_tile_set(source), tile_set_cooked_magic,
                                      tile_set_cooked_metadata_limit, 0, tile_set_cooked_version);
    const auto old_convex = decode_cooked_tile_set(
        asset_detail::encode_envelope(convex_envelope.metadata, {}, tile_set_cooked_magic,
                                      tile_set_cooked_metadata_limit, 2),
        source.asset(), 2);
    require(old_convex.tile(4).collision == definitions.tile(4).collision,
            "Legacy cooked2 convex data changed");
    const auto polygon_envelope =
        asset_detail::decode_envelope(cook_tile_set(concave_source), tile_set_cooked_magic,
                                      tile_set_cooked_metadata_limit, 0, tile_set_cooked_version);
    reject([&] {
        (void)decode_cooked_tile_set(
            asset_detail::encode_envelope(polygon_envelope.metadata, {}, tile_set_cooked_magic,
                                          tile_set_cooked_metadata_limit, 2),
            source.asset(), 2);
    });
    source.document["version"] = 1; // All collision fields are opaque in this legacy source.
    const auto legacy = decode_cooked_tile_set(cook_tile_set(source), source.asset());
    const Cells cells{{{selected_layer, {-1, 0}}, {{0, {2, 0}}, {1, {3, 4}}, {2, {4, 7}}}}};
    const auto map = map_value(cells, source.asset());
    const auto geometry = tile_map_collision_geometry(map, legacy, selected_layer);
    require(geometry.polygons.empty() &&
                geometry.rectangles == tile_map_collision_rectangles(map, selected_layer),
            "Legacy full-cell collision projection changed");
    reject(
        [&] { (void)tile_map_collision_geometry(map_value(cells), definitions, selected_layer); });
    Cells invalid{{{selected_layer, {0, 0}}, {{0, {99, 0}}}}};
    reject([&] {
        (void)tile_map_collision_geometry(map_value(invalid, source.asset()), legacy,
                                          selected_layer);
    });
    Cells maximum;
    for (std::int32_t chunk = 0; chunk < 4; ++chunk)
        for (std::uint32_t slot = 0; slot < tile_chunk_side * tile_chunk_side; ++slot)
            maximum[{selected_layer, {chunk, 0}}][slot] = {4, slot % 8};
    require(
        tile_map_collision_geometry(map_value(maximum, source.asset()), definitions, selected_layer)
                .polygons.size() == 4096,
        "Maximum custom collision shape count rejected");
    maximum[{selected_layer, {4, 0}}][0] = {4, 0};
    reject([&] {
        (void)tile_map_collision_geometry(map_value(maximum, source.asset()), definitions,
                                          selected_layer);
    });
}
} // namespace
int main() {
    try {
        simple_and_edges();
        random_and_capacity();
        definition_shapes();
        std::cout << "Tile Map collision rectangle coverage/bounds/capacity PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
