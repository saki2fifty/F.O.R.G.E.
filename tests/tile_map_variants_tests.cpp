#include "tile_map_collision.hpp"
#include "tile_map_cook.hpp"
#include "tile_map_projection_fixture.hpp"
#include "tile_map_variants.hpp"
#include "tile_neighbors_source_tests.hpp"
#include <chrono>
#include <iostream>

using namespace forge;
namespace {
using test_tile_neighbors_source::check;
std::shared_ptr<const TileSetData> definitions() {
    auto source = test_tile_neighbors_source::fixture();
    source.document["tiles"][0]["id"] = 9;
    source.document["next_id"] = 10;
    source.set_autotile(9, test_tile_neighbors_source::profile());
    source.set_collision(3, {{"kind", "none"}});
    source.set_collision(4, {{"kind", "convex"}, {"points", {{0., 0.}, {1., 0.}, {.75, 1.}}}});
    source.set_animation(
        4, {{"enabled", true},
            {"frames", {{{"frame", 2}, {"seconds", .125}}, {{"frame", 3}, {"seconds", .25}}}}});
    return std::make_shared<const TileSetData>(
        decode_cooked_tile_set(cook_tile_set(source), source.asset()));
}
std::shared_ptr<const TileMapData> small(AssetId set, std::span<const TileMapCellEdit> writes) {
    auto value = TileMapValue::create(AssetId::generate(), set);
    auto metadata = value.metadata();
    metadata["next_id"] = 3;
    metadata["layers"].push_back({{"id", 2}, {"name", "Other"}, {"visible", true}, {"order", 1}});
    value = value.with_metadata(metadata).paint(writes);
    return std::make_shared<const TileMapData>(
        decode_cooked_tile_map(cook_tile_map(value), value.asset()));
}
std::uint64_t selected(const TileMapVariants& view, std::uint64_t layer, TileCellAddress at) {
    const auto raw = view.source().cell(layer, at);
    const auto address = tile_chunk_cell(at);
    const auto values = view.overrides({layer, address.chunk});
    const auto found =
        std::lower_bound(values.begin(), values.end(), address.index,
                         [](const auto& value, auto slot) { return value.slot < slot; });
    return found != values.end() && found->slot == address.index ? found->tile : raw.tile;
}
} // namespace
int main() {
    try {
        {
            auto retiring = definitions();
            auto alias = std::make_shared<const TileSetData>(*retiring);
            const std::array<TileMapCellEdit, 1> writes{{{1, {}, {9, 0}}}};
            const auto raw = small(retiring->asset(), writes);
            const auto first = resolve_tile_variants(raw, retiring);
            const auto second = resolve_tile_variants(raw, alias, first);
            const std::array<const TileMapVariants*, 2> views{first.get(), second.get()};
            const auto baseline = TileMapVariants::resident_bytes(views);
            const auto rows = retiring->resident_bytes() - sizeof(TileSetData);
            retiring.reset();
            check(TileMapVariants::resident_bytes(views) == baseline,
                  "Shared live definition rows were charged as detached storage");
            alias.reset();
            check(TileMapVariants::resident_bytes(views) == baseline + rows &&
                      selected(*first, 1, {}) == 4 && selected(*second, 1, {}) == 4,
                  "Retired definition rows were lost or omitted/double-counted in derived storage");
        }
        const auto set = definitions();
        for (unsigned flags = 0; flags < 8; ++flags) {
            const std::array<TileMapCellEdit, 3> writes{
                {{1, {-1, -1}, {9, flags}}, {1, {-1, 0}, {9, 0}}, {2, {-1, -1}, {9, 0}}}};
            const auto raw = small(set->asset(), writes);
            const auto view = resolve_tile_variants(raw, set);
            check(view && view->matches(raw.get(), set.get()), "Variant original scope was lost");
            const auto geometry = tile_map_collision_geometry(*raw, *set, 1, 4096, view.get());
            check(geometry == tile_map_collision_geometry(*raw, *set, 1) &&
                      geometry.rectangles.empty() &&
                      geometry.polygons.size() == (flags <= 1 ? 1 : 2),
                  "Collision and neighbor selection disagree across orientation/chunk seam");
            check(tile_map_collision_geometry(*raw, *set, 2, 4096, view.get()).polygons.size() == 1,
                  "Collision neighbor selection leaked across layers");
            const std::array<TileMapCellEdit, 1> erase{{{1, {-1, 0}, {}}}};
            const auto changed = std::make_shared<const TileMapData>(raw->with_cells(erase, *set));
            const auto next = resolve_tile_variants(changed, set, view);
            const auto edited_geometry =
                tile_map_collision_geometry(*changed, *set, 1, 4096, next.get());
            check(edited_geometry.rectangles.empty() && edited_geometry.polygons.size() == 1 &&
                      edited_geometry.polygons[0].origin == TileCellAddress{-1, -1} &&
                      changed->cell(1, {-1, -1}).tile == 9 && raw->cell(1, {-1, 0}).tile == 9,
                  "Neighbor edit did not update collision while preserving logical/retained cells");
            check(tile_map_collision_geometry(*changed, *set, 1, 4096, view.get()) ==
                      edited_geometry,
                  "Foreign derived collision view was consumed instead of rebuilding the matching "
                  "value");
            check(selected(*view, 1, {-1, -1}) == (flags <= 1 ? 3 : 4),
                  "Variant selection differs across orientation/negative chunk seam");
            check(selected(*view, 2, {-1, -1}) == 4, "Neighbor rule connected across layers");
            check(raw->cell(1, {-1, -1}).tile == 9 && view->source().cell(1, {-1, -1}).tile == 9,
                  "Derived selection rewrote logical cells");
            check(resolve_tile_variants(raw, set, view) == view,
                  "Unchanged selection rebuilt storage");
        }
        const auto raw = std::make_shared<const TileMapData>(
            test_tile_projection::profile(AssetId::generate(), set->asset(), 1024, 256));
        const auto start = std::chrono::steady_clock::now();
        const auto view = resolve_tile_variants(raw, set);
        const auto initial_time = std::chrono::steady_clock::now() - start;
        const std::array views{view.get()};
        const auto retained = TileMapVariants::resident_bytes(views);
        check(view->recomputed_chunks() == 1024 && raw->occupied() == tile_map_cell_limit &&
                  retained < 6 * 1024 * 1024,
              "Maximum sparse variant selection exceeded compact storage envelope");
        view->for_each_chunk([&](auto, auto cells, auto occupied) {
            check(occupied == 256, "Selected chunk occupancy changed");
            for (unsigned slot = 0; slot < 1024; ++slot)
                check(cells[slot].tile == (slot >= 256   ? 0
                                           : slot >= 224 ? 4
                                                         : 3),
                      "Maximum selected cell differs from independent row oracle");
        });
        const auto old_far = view->overrides({1, {1000, 0}}).data();
        const std::array<TileMapCellEdit, 1> erase{{{1, {0, 7}, {}}}};
        const auto changed = std::make_shared<const TileMapData>(raw->with_cells(erase, *set));
        const auto incremental_start = std::chrono::steady_clock::now();
        const auto incremental = resolve_tile_variants(changed, set, view);
        const auto incremental_time = std::chrono::steady_clock::now() - incremental_start;
        const auto full_start = std::chrono::steady_clock::now();
        const auto full = resolve_tile_variants(changed, set);
        const auto full_time = std::chrono::steady_clock::now() - full_start;
        check(incremental->recomputed_chunks() == 2 && full->recomputed_chunks() == 1024 &&
                  incremental->overrides({1, {1000, 0}}).data() == old_far &&
                  incremental->overrides({1, {1, 0}}).data() == view->overrides({1, {1, 0}}).data(),
              "One-cell edit lost unchanged/halo output reuse");
        check(selected(*incremental, 1, {0, 6}) == 4 && selected(*view, 1, {0, 6}) == 3 &&
                  selected(*incremental, 1, {0, 7}) == 0,
              "Neighbor edit was not reflected or mutated the retained view");
        incremental->source().for_each_chunk([&](auto key, auto, auto) {
            const auto a = incremental->overrides(key), b = full->overrides(key);
            check(std::equal(a.begin(), a.end(), b.begin(), b.end()),
                  "Incremental variant output differs from fresh full resolution");
        });
        const std::array pair{view.get(), incremental.get()};
        check(TileMapVariants::resident_bytes(pair) < retained + 256 * 1024,
              "Retained delta copied the full selection payload");
        auto ephemeral = small(set->asset(), std::array<TileMapCellEdit, 1>{{{1, {}, {9, 0}}}});
        const auto detached = resolve_tile_variants(ephemeral, set);
        ephemeral.reset();
        check(selected(*detached, 1, {}) == 4 && !detached->matches(nullptr, set.get()),
              "Detached selected values depended on retired originals");
        std::cout << "Compact variants PASS: cells=" << raw->occupied()
                  << " raw_bytes=" << raw->resident_bytes() << " extra_bytes=" << retained
                  << " initial_ms="
                  << std::chrono::duration<double, std::milli>(initial_time).count()
                  << " full_after_edit_ms="
                  << std::chrono::duration<double, std::milli>(full_time).count()
                  << " incremental_ms="
                  << std::chrono::duration<double, std::milli>(incremental_time).count()
                  << " (single Linux CPU sample; no hardware-render performance claim)\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
