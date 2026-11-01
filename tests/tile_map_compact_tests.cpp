#include "tile_map_archive.hpp"
#include "tile_map_collision.hpp"
#include "tile_map_cook.hpp"
#include "tile_map_projection_fixture.hpp"
#include "tile_map_variants.hpp"
#include "tile_map_view.hpp"
#include <iostream>
#include <map>

using namespace forge;
namespace {
using test_tile_projection::check;
using test_tile_projection::rejects;
auto identities(const TileMapData& data) {
    std::map<TileMapChunkKey, const void*> result;
    data.for_each_chunk_info([&](auto key, auto, const void* token) { result[key] = token; });
    return result;
}
void compare(const TileMapData& a, const TileMapData& b) {
    check(a.asset() == b.asset() && a.tile_set() == b.tile_set() && a.layers() == b.layers() &&
              a.cell_size() == b.cell_size() && a.occupied() == b.occupied() &&
              a.chunk_count() == b.chunk_count(),
          "Compact and full runtime metadata differ");
    std::size_t visited = 0;
    a.for_each_chunk_values([&](auto key, auto cells, auto count) {
        std::size_t occupied = 0;
        for (unsigned slot = 0; slot < cells.size(); ++slot) {
            const auto address = tile_cell_address({key.address, slot});
            check(cells[slot] == a.cell(key.layer, address) &&
                      cells[slot] == b.cell(key.layer, address),
                  "Scoped compact values differ from exact full/sparse queries");
            occupied += bool(cells[slot].tile);
        }
        check(occupied == count, "Compact chunk metadata has a different occupancy");
        visited += occupied;
    });
    check(visited == a.occupied(), "Compact scoped iteration missed logical cells");
}
void edits_and_lifetime() {
    const auto set = test_tile_projection::definitions(AssetId::generate());
    auto source = TileMapValue::create(AssetId::generate(), set.asset());
    const auto wide = test_tile_projection::wide;
    const std::array<TileMapCellEdit, 4> seed{{{1, {}, {9, 0}},
                                               {1, {31, 31}, {wide, 7}},
                                               {1, {-1, -1}, {9, 2}},
                                               {1, {INT32_MAX, INT32_MIN}, {wide, 3}}}};
    source = source.paint(seed);
    auto archive = decode_tile_map_archive(cook_tile_map(source), source.asset());
    const auto dense = archive.materialize();
    auto compact = archive.compact();
    archive = {};
    const auto before = compact.resident_bytes();
    check(compact.dense_reconstruction_bytes() >= dense.resident_bytes() &&
              dense.dense_reconstruction_bytes() == dense.resident_bytes(),
          "Compact checkpoint charge underestimates independent dense reconstruction");
    compare(compact, dense);
    check(compact.resident_bytes() == before, "Scoped reads retained hidden dense chunk arrays");
    std::size_t borrowed_visits = 0;
    rejects([&] { compact.for_each_chunk([&](auto, auto, auto) { ++borrowed_visits; }); });
    check(!borrowed_visits, "Borrowed-span API exposed temporary compact chunk storage");
    const auto tokens = identities(compact);
    const auto no_op = compact.with_cells(seed, set);
    check(identities(no_op) == tokens,
          "A no-op compact edit decoded or replaced unchanged immutable chunks");
    const std::array<TileMapCellEdit, 4> writes{
        {{1, {}, {wide, 1}}, {1, {}, {9, 6}}, {1, {-1, -1}, {}}, {1, {64, 0}, {wide, 4}}}};
    const auto edited = compact.with_cells(writes, set);
    compare(edited, dense.with_cells(writes, set));
    check(compact.cell(1, {}) == TileMapCell{9, 0} &&
              compact.cell(1, {-1, -1}) == TileMapCell{9, 2} &&
              edited.cell(1, {}) == TileMapCell{9, 6} &&
              edited.cell(1, {-1, -1}) == TileMapCell{} &&
              edited.cell(1, {64, 0}) == TileMapCell{wide, 4},
          "Compact COW edits changed the original or lost ordered last-write behavior");
    const auto changed = identities(edited);
    const auto untouched = tile_chunk_cell({INT32_MAX, INT32_MIN}).chunk;
    check(changed.at({1, untouched}) == tokens.at({1, untouched}) &&
              changed.at({1, {0, 0}}) != tokens.at({1, {0, 0}}),
          "Compact edits replaced an untouched chunk or retained changed storage identity");
    auto copy = compact;
    const std::array<const TileMapData*, 2> copies{&compact, &copy};
    check(TileMapData::resident_bytes(copies) < 2 * before,
          "Copied compact views double-charged shared sparse backing");
    auto erased = seed;
    for (auto& edit : erased)
        edit.value = {};
    const auto empty = compact.with_erased_cells(erased);
    check(!empty.occupied() && !empty.chunk_count() && empty.cell(1, {}) == TileMapCell{} &&
              empty.resident_bytes() < before / 2,
          "Erasing all compact chunks retained unavailable backing or logical cells");
    rejects([&] { (void)compact.with_erased_cells(seed); });
    rejects([&] { compact.for_each_chunk_info({}); });
    rejects([&] { compact.for_each_chunk_values({}); });
}
void consumer_views() {
    const auto set_id = AssetId::generate();
    const auto set =
        std::make_shared<const TileSetData>(test_tile_projection::definitions(set_id, true, true));
    auto source = TileMapValue::create(AssetId::generate(), set_id);
    const std::array<TileMapCellEdit, 4> cells{{{1, {-1, 0}, {9, 0}},
                                                {1, {}, {9, 7}},
                                                {1, {31, 0}, {9, 3}},
                                                {1, {32, 0}, {test_tile_projection::wide, 0}}}};
    source = source.paint(cells);
    const auto archive = decode_tile_map_archive(cook_tile_map(source), source.asset());
    const auto compact = std::make_shared<const TileMapData>(archive.compact());
    const auto dense = std::make_shared<const TileMapData>(archive.materialize());
    const auto compact_geometry = tile_map_collision_geometry(*compact, *set, 1);
    check(!compact_geometry.polygons.empty() &&
              compact_geometry == tile_map_collision_geometry(*dense, *set, 1),
          "Compact values changed selected concave physical geometry");
    const auto neighbors = std::make_shared<const TileSetData>(
        test_tile_projection::definitions(set_id, true, true, true, true));
    const auto compact_selected = resolve_tile_variants(compact, neighbors);
    const auto dense_selected = resolve_tile_variants(dense, neighbors);
    check(compact_selected && dense_selected &&
              compact_selected->recomputed_chunks() == dense_selected->recomputed_chunks(),
          "Compact neighbor selection lost chunk metadata or halo evidence");
    for (const auto& edit : cells) {
        const auto key = TileMapChunkKey{edit.layer, tile_chunk_cell(edit.address).chunk};
        const auto a = compact_selected->overrides(key), b = dense_selected->overrides(key);
        check(std::equal(a.begin(), a.end(), b.begin(), b.end()),
              "Compact and dense selected neighbor rows differ across a chunk seam");
    }
    check(tile_map_projection_header(*compact, *neighbors) ==
              tile_map_projection_header(*dense, *neighbors),
          "Compact storage changed copied Play header bytes");
    const auto keys = tile_map_projection_changes(*compact);
    check(keys == tile_map_projection_changes(*dense) &&
              tile_map_projection_page(*compact, keys) == tile_map_projection_page(*dense, keys),
          "Compact storage changed copied Play full-page keys or cell bytes");
    const auto header = decode_tile_map_projection_header(tile_map_projection_header(*dense, *set),
                                                          dense->asset(), set->asset());
    const auto copied = apply_tile_map_projection_page(
        header.empty, *set, tile_map_projection_page(*dense, keys), true);
    compare(copied, *dense);
    check(copied.chunk_input(keys.front()).encoded_cell_records().has_value() &&
              copied.resident_bytes() < dense->resident_bytes(),
          "Copied compact pages retained fully decoded source chunks");
    const auto unchanged = compact->with_cells(cells, *set);
    check(tile_map_projection_changes(unchanged, compact.get()).empty(),
          "No-op compact writes produced a copied Play structural delta");
    const std::array<TileMapCellEdit, 1> changed{{{1, {}, {9, 6}}}};
    const auto next = compact->with_cells(changed, *set);
    check(tile_map_projection_changes(next, compact.get()) ==
              std::vector<TileMapChunkKey>{{1, {0, 0}}},
          "Compact COW change invalidated another copied Play chunk");
    Camera camera;
    camera.projection = std::uint32_t(CameraProjection::Orthographic);
    camera.orthographic_height = 16;
    AffineTransform transform;
    transform.m[3] = transform.m[7] = 16;
    transform.m[11] = -5;
    const std::array views{camera_view(camera, transform, 128, 128)};
    const auto retained = compact->resident_bytes();
    std::vector<TileMapChunkKey> visible;
    visit_visible_tile_chunk_info(*compact, {}, views, [&](const auto& info) {
        check(info.occupied == 2, "Compact culling read a different chunk occupancy");
        visible.push_back(info.key);
    });
    check(visible == std::vector<TileMapChunkKey>{{1, {0, 0}}},
          "Metadata-only compact culling decoded or selected off-camera chunks");
    std::size_t scoped = 0;
    visit_visible_tile_chunk_values(*compact, {}, views, [&](const auto& chunk) {
        check(chunk.key == visible.front() && chunk.cells[0] == TileMapCell{9, 7},
              "Scoped visible compact cells differ from exact logical queries");
        ++scoped;
    });
    check(scoped == 1 && compact->resident_bytes() == retained,
          "Camera value access retained hidden dense chunk residency");
    bool called = false;
    check(!compact->visit_chunk_values({1, {100, 100}}, [&](auto, auto) { called = true; }) &&
              !called,
          "Absent logical chunk invoked a scoped cell visitor");
    rejects([&] { compact->visit_chunk_values({2, {}}, [](auto, auto) {}); });
    rejects([&] { visit_visible_tile_chunks(*compact, {}, views, [](const auto&) {}); });
}
void reconstruction_metadata() {
    auto source = TileMapValue::create(AssetId::generate(), AssetId::generate());
    auto metadata = source.metadata();
    metadata["layers"][0]["name"] = std::string(128, 'A');
    metadata["next_id"] = 9;
    for (std::uint64_t layer = 2; layer <= tile_map_layer_limit; ++layer)
        metadata["layers"].push_back({{"id", layer},
                                      {"name", std::string(128, char('A' + layer))},
                                      {"visible", bool(layer % 2)},
                                      {"order", -7}});
    source = source.with_metadata(std::move(metadata));
    const std::array<TileMapCellEdit, 2> edits{{{1, {}, {9, 0}}, {8, {-1, -1}, {9, 7}}}};
    source = source.paint(edits);
    const auto archive = decode_tile_map_archive(cook_tile_map(source), source.asset());
    const auto compact = archive.compact(), dense = archive.materialize();
    check(compact.layers().size() == 8 &&
              compact.dense_reconstruction_bytes() >= dense.resident_bytes(),
          "Full-layer long-name metadata escaped dense reconstruction accounting");
}
} // namespace
int main() {
    try {
        edits_and_lifetime();
        consumer_views();
        reconstruction_metadata();
        std::cout << "Compact runtime values, exact queries, scoped decoding, COW edits and shared "
                     "accounting passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
