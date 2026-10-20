#include "cooked_envelope.hpp"
#include "tile_map_view.hpp"
#include "tile_set_data.hpp"
#include <iostream>

using namespace forge;
namespace {
// UINT64_MAX is the allocation high-water ceiling; entries must be below it.
constexpr auto wide_tile_key = UINT64_MAX - 1;
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void reject(F&& action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid runtime tile cell batch was accepted");
}
auto chunks(const TileMapData& data) {
    std::map<TileMapChunkKey, const TileMapCell*> result;
    data.for_each_chunk([&](auto key, auto cells, auto) { result[key] = cells.data(); });
    return result;
}
TileMapData profile(AssetId id, AssetId set, unsigned cells_per_chunk) {
    std::vector<std::byte> payload;
    payload.reserve(tile_map_chunk_limit * (20 + cells_per_chunk * 16));
    for (std::uint32_t x = 0; x < tile_map_chunk_limit; ++x) {
        for (const auto scalar : {1u, 0u, x, 0u, cells_per_chunk})
            asset_detail::cooked_write32(payload, scalar);
        for (std::uint32_t slot = 0; slot < cells_per_chunk; ++slot)
            for (const auto scalar : {slot, 9u, 0u, 0u})
                asset_detail::cooked_write32(payload, scalar);
    }
    const nlohmann::json root{
        {"asset_id", id},
        {"tile_set", set},
        {"cell_size", {1., 1.}},
        {"chunk_count", tile_map_chunk_limit},
        {"cell_count", tile_map_chunk_limit * cells_per_chunk},
        {"layers",
         {{{"id", 1}, {"name", "Runtime limit fixture"}, {"visible", true}, {"order", 0}}}}};
    return decode_cooked_tile_map(asset_detail::encode_envelope(root, payload,
                                                                tile_map_cooked_magic,
                                                                tile_map_cooked_metadata_limit),
                                  id);
}
void edit_candidates(const TileMapData& data, const TileSetData& definitions) {
    const auto baseline = chunks(data);
    const std::array<TileMapCellEdit, 4> writes{{{1, {-29, 0}, {9, 2}},
                                                 {1, {INT32_MIN, INT32_MAX}, {wide_tile_key, 7}},
                                                 {1, {}, {9, 0}},
                                                 {1, {}, {wide_tile_key, 4}}}};
    const auto first = data.with_cells(writes, definitions);
    data.validate_cells(writes, definitions);
    require(chunks(data) == baseline, "Validation changed a runtime snapshot");
    require(data.occupied() == 1 && first.occupied() == 3 && first.chunk_count() == 3 &&
                first.cell(1, {}) == TileMapCell{wide_tile_key, 4} &&
                first.cell(1, {INT32_MIN, INT32_MAX}) == TileMapCell{wide_tile_key, 7} &&
                data.cell(1, {}) == TileMapCell{},
            "Detached runtime candidates lost exact IDs, signed limits, ordering or instance "
            "isolation");
    require(chunks(first).at({1, {-1, 0}}) == baseline.at({1, {-1, 0}}),
            "Runtime candidate copied an unchanged chunk");
    const std::array<TileMapCellEdit, 1> erase{{{1, {}, {}}}};
    const auto erased = first.with_cells(erase, definitions);
    require(erased.occupied() == 2 && erased.chunk_count() == 2 && first.occupied() == 3,
            "Runtime erase retained an empty chunk or changed an earlier snapshot");
    const std::array<TileMapCellEdit, 1> change{{{1, {-29, 0}, {9, 3}}}};
    const auto changed = data.with_cells(change, definitions);
    require(chunks(changed).at({1, {-1, 0}}) != baseline.at({1, {-1, 0}}) &&
                data.cell(1, {-29, 0}) == TileMapCell{9, 2},
            "Runtime mutation rewrote shared cell storage");
    for (const auto invalid :
         {TileMapCellEdit{2, {}, {9, 0}}, {1, {}, {8, 0}}, {1, {}, {9, 8}}, {1, {}, {0, 1}}}) {
        const std::array batch{change[0], invalid};
        reject([&] { (void)data.with_cells(batch, definitions); });
        reject([&] { data.validate_cells(batch, definitions); });
    }
    std::vector<TileMapCellEdit> excessive(tile_map_edit_limit + 1, change[0]);
    reject([&] { (void)data.with_cells(excessive, definitions); });
    reject([&] { (void)TileMapData{}.with_cells({}, definitions); });
    reject([&] { (void)data.with_cells({}, TileSetData{}); });
    require(chunks(data) == baseline && data.cell(1, {-29, 0}) == TileMapCell{9, 2},
            "Rejected runtime batch changed its baseline");

    const auto maximum = profile(data.asset(), definitions.asset(), 256);
    const auto maximum_pointers = chunks(maximum);
    const std::array<TileMapCellEdit, 1> overflow{{{1, {0, 8}, {9, 0}}}};
    reject([&] { (void)maximum.with_cells(overflow, definitions); });
    reject([&] { maximum.validate_cells(overflow, definitions); });
    const std::array<TileMapCellEdit, 2> move_cell{{overflow[0], {1, {}, {}}}};
    const auto moved = maximum.with_cells(move_cell, definitions);
    maximum.validate_cells(move_cell, definitions);
    require(moved.occupied() == tile_map_cell_limit &&
                moved.chunk_count() == tile_map_chunk_limit && moved.cell(1, {}) == TileMapCell{} &&
                moved.cell(1, {0, 8}) == TileMapCell{9, 0},
            "Atomic runtime cell move rejected a valid final state at capacity");
    auto moved_pointers = chunks(moved);
    moved_pointers.erase({1, {0, 0}});
    for (const auto& [key, pointer] : moved_pointers)
        require(pointer == maximum_pointers.at(key), "Small edit copied another runtime chunk");
    const auto sparse = profile(data.asset(), definitions.asset(), 1);
    const std::array<TileMapCellEdit, 1> new_chunk{{{1, {32 * 1024, 0}, {9, 0}}}};
    reject([&] { (void)sparse.with_cells(new_chunk, definitions); });
    const std::array<TileMapCellEdit, 2> move_chunk{{new_chunk[0], {1, {}, {}}}};
    const auto relocated = sparse.with_cells(move_chunk, definitions);
    sparse.validate_cells(move_chunk, definitions);
    require(relocated.chunk_count() == tile_map_chunk_limit && relocated.occupied() == 1024 &&
                relocated.cell(1, {}) == TileMapCell{} &&
                relocated.cell(1, {32 * 1024, 0}) == TileMapCell{9, 0},
            "Atomic runtime chunk move rejected a valid final state at capacity");
    std::vector<TileMapCellEdit> unchanged;
    for (unsigned repeat = 0; repeat < 4; ++repeat)
        for (std::int32_t x = 0; x < 1024; ++x)
            unchanged.push_back({1, {x * 32, 0}, {9, 0}});
    require(chunks(maximum.with_cells(unchanged, definitions)) == maximum_pointers &&
                chunks(maximum.with_cells({}, definitions)) == maximum_pointers,
            "No-op runtime batch copied stable chunk arrays");
}
} // namespace
int main() {
    try {
        const auto id = AssetId::parse("c473e29a-b537-4f18-8de2-561bda973604");
        const auto tile_set = AssetId::parse("9cf0f8e9-77b2-42d2-93f9-354da275a9b6");
        // One hand-authored binary record: layer1, chunk(-1,0), slot3, tile9,
        // flags2. This consumer links no Tile Map source/cooker/document library.
        std::vector<std::byte> payload;
        for (const auto scalar : {1u, 0u, UINT32_MAX, 0u, 1u, 3u, 9u, 0u, 2u})
            asset_detail::cooked_write32(payload, scalar);
        const nlohmann::json metadata{
            {"asset_id", id},
            {"tile_set", tile_set},
            {"cell_size", {1., 1.}},
            {"chunk_count", 1},
            {"cell_count", 1},
            {"layers",
             {{{"id", 1}, {"name", "Original runtime layer"}, {"visible", true}, {"order", 0}}}}};
        const auto bytes = asset_detail::encode_envelope(metadata, payload, tile_map_cooked_magic,
                                                         tile_map_cooked_metadata_limit);
        const auto data = decode_cooked_tile_map(bytes, id);
        if (data.cell(1, {-29, 0}) != TileMapCell{9, 2} || data.asset() != id ||
            data.tile_set() != tile_set)
            throw std::runtime_error(
                "Cooked-only runtime cell/identity differs from binary fixture");
        const auto material = AssetId::parse("13a16254-bce4-41d3-b58f-d58399e0b2d4");
        const nlohmann::json set_metadata{
            {"asset_id", tile_set},
            {"images",
             {{{"id", 1},
               {"name", "Original image"},
               {"material", material},
               {"grid",
                {{"columns", 1},
                 {"rows", 1},
                 {"margin_x", 0},
                 {"margin_y", 0},
                 {"spacing_x", 0},
                 {"spacing_y", 0}}}}}},
            {"tiles",
             {{{"id", 9}, {"name", "Original tile"}, {"image", 1}, {"frame", 0}},
              {{"id", wide_tile_key}, {"name", "Wide local key"}, {"image", 1}, {"frame", 0}}}}};
        const auto set_bytes = asset_detail::encode_envelope(
            set_metadata, {}, tile_set_cooked_magic, tile_set_cooked_metadata_limit);
        const auto definitions = decode_cooked_tile_set(set_bytes, data.tile_set());
        edit_candidates(data, definitions);
        auto wrong_root = set_metadata;
        const auto other_set = AssetId::generate();
        wrong_root["asset_id"] = other_set;
        const auto unrelated = decode_cooked_tile_set(
            asset_detail::encode_envelope(wrong_root, {}, tile_set_cooked_magic,
                                          tile_set_cooked_metadata_limit),
            other_set);
        reject([&] { (void)data.with_cells({}, unrelated); });
        const auto& tile = definitions.tile(data.cell(1, {-29, 0}).tile);
        if (definitions.image(tile.image).material.id != material || tile.frame != 0)
            throw std::runtime_error(
                "Runtime-only map/tile/image/Material reference chain differs");
        Camera camera;
        camera.projection = std::uint32_t(CameraProjection::Orthographic);
        camera.orthographic_height = 40;
        AffineTransform world;
        world.m[3] = -16;
        world.m[7] = 16;
        world.m[11] = -5;
        const std::array<CameraView, 1> cameras{camera_view(camera, world, 400, 400)};
        unsigned chunks = 0;
        visit_visible_tile_chunks(data, {}, cameras, [&](const TileMapChunkView& view) {
            if (view.key != TileMapChunkKey{1, {-1, 0}} || view.cells[3] != TileMapCell{9, 2})
                throw std::runtime_error("Cooked-only runtime immutable view differs");
            ++chunks;
        });
        if (chunks != 1)
            throw std::runtime_error("Cooked-only runtime did not project exactly one chunk");
        std::cout << "Cooked-only runtime map edits, capacities, immutable isolation and camera "
                     "view passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
