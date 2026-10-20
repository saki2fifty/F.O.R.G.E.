#include "tile_map_paint.hpp"
#include "tile_set_cook.hpp"
#include <iostream>

using namespace forge;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void reject(F action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Expected stale/invalid paint gesture rejection");
}
std::set<TileCellAddress> occupied(const TileMapValue& map) {
    std::set<TileCellAddress> result;
    map.for_each_chunk([&](auto key, auto cells, auto) {
        for (std::uint32_t i = 0; i < tile_chunk_cells; ++i)
            if (cells[i].tile)
                result.insert(tile_cell_address({key.address, i}));
    });
    return result;
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Expected scratch directory");
        const std::filesystem::path root = argv[1];
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root / "Assets");
        auto lease = std::make_shared<ProjectLease>(root);
        auto set = TileSetSource::create(AssetId::generate());
        set.document["next_id"] = 3;
        set.document["images"] = {{{"id", 1},
                                   {"name", "Original sheet"},
                                   {"material", AssetId::generate()},
                                   {"grid",
                                    {{"columns", 1},
                                     {"rows", 1},
                                     {"margin_x", 0},
                                     {"margin_y", 0},
                                     {"spacing_x", 0},
                                     {"spacing_y", 0}}}}};
        set.document["tiles"] = {{{"id", 2}, {"name", "Tile"}, {"image", 1}, {"frame", 0}}};
        const auto definitions = decode_cooked_tile_set(cook_tile_set(set), set.asset());
        const auto create = [&](const char* name) {
            return TileMapDocument::create(
                lease, std::filesystem::path("Assets") / name,
                [&](AssetId id) { return TileMapSource{TileMapValue::create(id, set.asset())}; });
        };
        auto document = create("Paint.tilemap.json");
        const auto original = document->source().document;
        const auto disk =
            asset_detail::read_bytes(root / document->locator(), tile_map_source_byte_limit);
        TileMapPaintStroke stroke(*document, definitions, 1, {2, 7});
        check(stroke.sample({-33, -33}) && stroke.sample({33, 33}) && stroke.cell_count() == 67,
              "Paint interpolation lost negative chunk seams or endpoints");
        check(document->source().document == original && !document->dirty() &&
                  !document->can_undo(),
              "Unreleased stroke mutated the authored document/history");
        stroke.commit();
        const auto painted = document->source().document;
        check(painted.occupied() == 67 && painted.cell(1, {-32, -32}) == TileMapCell{2, 7},
              "Released stroke did not apply its preview/flags");
        document->undo();
        check(document->source().document == original && !document->can_undo() &&
                  document->can_redo(),
              "A drag produced more than one Undo step");
        const auto revision = document->revision();
        TileMapPaintStroke canceled(*document, definitions, 1, {2, 0});
        check(canceled.sample({9, 9}), "Cancel fixture sample failed");
        canceled.cancel();
        check(document->revision() == revision && document->can_redo() && !document->dirty(),
              "Cancel changed revision, saved state or Redo");
        reject([&] { canceled.commit(); });
        document->redo();
        check(document->source().document == painted, "Redo did not restore a complete stroke");
        TileMapPaintStroke erase(*document, 1);
        check(erase.sample({-33, -33}) && erase.sample({33, 33}) &&
                  !erase.preview().document.occupied(),
              "Erase did not clear the interpolated cells");
        erase.commit();
        document->undo();
        check(document->source().document == painted, "Erase Undo did not retain old cells");
        check(asset_detail::read_bytes(root / document->locator(), tile_map_source_byte_limit) ==
                  disk,
              "Paint/Undo/Redo wrote the source before Save");
        auto bounded = create("Bounded.tilemap.json");
        TileMapPaintStroke full(*bounded, definitions, 1, {2, 0});
        check(full.sample({0, 0}) && full.sample({4095, 0}) && !full.sample({4096, 0}) &&
                  full.cell_count() == tile_map_edit_limit && full.delta().empty(),
              "Stroke limit partially accepted an overflowing segment");
        full.commit();
        bounded->undo();
        check(!bounded->source().document.occupied(), "Full stroke lost Undo");
        bounded->redo();
        check(bounded->source().document.occupied() == tile_map_edit_limit,
              "Full stroke lost Redo");
        TileMapPaintStroke stale(*bounded, definitions, 1, {2, 0});
        bounded->undo();
        reject([&] { stale.sample({0, 0}); });
        reject([&] { stale.commit(); });
        TileMapPaintStroke edge(*bounded, definitions, 1, {2, 0});
        check(edge.sample({INT32_MIN, INT32_MIN}) && edge.sample({INT32_MIN + 1, INT32_MIN + 3}) &&
                  !edge.sample({INT32_MAX, INT32_MAX}) && edge.cell_count() == 4,
              "Full-range interpolation overflowed or accepted an unbounded jump");
        edge.cancel();
        TileMapPaintStroke forward(*bounded, definitions, 1, {2, 0}),
            reverse(*bounded, definitions, 1, {2, 0});
        check(forward.sample({-31, 5}) && forward.sample({40, -12}) && reverse.sample({40, -12}) &&
                  reverse.sample({-31, 5}) &&
                  occupied(forward.preview().document) == occupied(reverse.preview().document),
              "Raster ties changed cells when the pointer reversed direction");
        auto oversized = bounded->source();
        auto metadata = oversized.document.metadata();
        metadata["opaque"] = std::string(9 * 1024 * 1024, 'a');
        oversized.document = oversized.document.with_metadata(std::move(metadata));
        check(!bounded->retains_history(oversized),
              "History preflight promised Redo for an oversized reverse snapshot");
        reject([&] { TileMapPaintStroke invalid(*bounded, definitions, 99, {2, 0}); });
        reject([&] { TileMapPaintStroke invalid(*bounded, definitions, 1, {99, 0}); });
        std::cout << "Tile paint gestures, atomic limits, cancel and document Undo/Redo passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
