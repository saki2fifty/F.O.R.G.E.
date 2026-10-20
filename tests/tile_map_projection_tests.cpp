#include "cooked_envelope.hpp"
#include "tile_map_play_projection.hpp"
#include "tile_map_projection.hpp"
#include <bit>
#include <iostream>
using namespace forge;
namespace {
using Json = nlohmann::json;
constexpr auto wide = UINT64_MAX - 1;
void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F> void rejects(F action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Malformed tile presentation was accepted");
}
TileSetData definitions(AssetId id) {
    const Json root{{"asset_id", id},
                    {"images",
                     {{{"id", 1},
                       {"name", "Original"},
                       {"material", AssetId::generate()},
                       {"grid",
                        {{"columns", 2},
                         {"rows", 1},
                         {"margin_x", 0},
                         {"margin_y", 0},
                         {"spacing_x", 0},
                         {"spacing_y", 0}}}}}},
                    {"tiles",
                     {{{"id", 9}, {"name", "Paint"}, {"image", 1}, {"frame", 0}},
                      {{"id", wide}, {"name", "Wide key"}, {"image", 1}, {"frame", 1}}}}};
    return decode_cooked_tile_set(asset_detail::encode_envelope(root, {}, tile_set_cooked_magic,
                                                                tile_set_cooked_metadata_limit),
                                  id);
}
TileMapData profile(AssetId id, AssetId set, unsigned count, unsigned occupied,
                    bool wide_keys = false) {
    std::vector<std::byte> payload;
    for (unsigned chunk = 0; chunk < count; ++chunk) {
        for (auto scalar : {1u, 0u, chunk, 0u, occupied})
            asset_detail::cooked_write32(payload, scalar);
        for (unsigned slot = 0; slot < occupied; ++slot) {
            const auto key = wide_keys ? wide : 9;
            for (auto scalar : {slot, std::uint32_t(key), std::uint32_t(key >> 32), 0u})
                asset_detail::cooked_write32(payload, scalar);
        }
    }
    const Json root{
        {"asset_id", id},
        {"tile_set", set},
        {"cell_size", {1., 1.}},
        {"chunk_count", count},
        {"cell_count", count * occupied},
        {"layers",
         {{{"id", 1}, {"name", "Projection fixture"}, {"visible", true}, {"order", -3}}}}};
    return decode_cooked_tile_map(asset_detail::encode_envelope(root, payload,
                                                                tile_map_cooked_magic,
                                                                tile_map_cooked_metadata_limit),
                                  id);
}
auto pointers(const TileMapData& map) {
    std::map<TileMapChunkKey, const TileMapCell*> out;
    map.for_each_chunk([&](auto key, auto cells, auto) { out[key] = cells.data(); });
    return out;
}
void equal(const TileMapData& actual, const TileMapData& expected) {
    check(actual.asset() == expected.asset() && actual.tile_set() == expected.tile_set() &&
              actual.cell_size() == expected.cell_size() && actual.layers() == expected.layers() &&
              actual.occupied() == expected.occupied() &&
              actual.chunk_count() == expected.chunk_count(),
          "Copied tile metadata/counts differ");
    const auto other = pointers(expected);
    actual.for_each_chunk([&](auto key, auto cells, auto) {
        check(other.contains(key) && std::equal(cells.begin(), cells.end(), other.at(key)),
              "Copied chunk contents differ");
    });
}
} // namespace
int main() {
    try {
        const auto id = AssetId::generate(), set = AssetId::generate();
        const auto defs = definitions(set);
        const auto maximum = profile(id, set, 1024, 256);
        const auto header =
            decode_tile_map_projection_header(tile_map_projection_header(maximum, defs), id, set);
        check(header.expected_cells == 262144 && header.expected_chunks == 1024 &&
                  !header.empty.occupied(),
              "Header inlined cells or lost maximum profile counts");
        const auto keys = tile_map_projection_changes(maximum);
        auto copied = header.empty;
        for (std::size_t at = 0; at < keys.size(); at += tile_projection_page_chunks) {
            const auto page = tile_map_projection_page(
                maximum, std::span(keys).subspan(
                             at, std::min(tile_projection_page_chunks, keys.size() - at)));
            check(page.dump().size() <= tile_projection_page_bytes,
                  "Full transfer page exceeded byte limit");
            copied = apply_tile_map_projection_page(copied, header.definitions, page);
        }
        equal(copied, maximum);
        const std::array<TileMapCellEdit, 1> edit{{{1, {0, 0}, {9, 7}}}};
        const auto changed = maximum.with_cells(edit, defs);
        const auto delta = tile_map_projection_changes(changed, &maximum);
        const std::array<const TileMapData*, 3> same{{&maximum, &maximum, &maximum}};
        const std::array<const TileMapData*, 2> shared{{&maximum, &changed}};
        check(TileMapData::resident_bytes(same) == maximum.resident_bytes() &&
                  TileMapData::resident_bytes(shared) > maximum.resident_bytes() &&
                  TileMapData::resident_bytes(shared) <
                      maximum.resident_bytes() + changed.resident_bytes(),
              "Copied view accounting duplicated shared chunks or omitted a changed value");
        check(TileMapData::resident_bytes({}) == 0, "Empty copied view budget was nonzero");
        check(delta.size() == 1 && tile_map_projection_changes(changed, &changed).empty(),
              "Unchanged or one-cell view retransmits the map");
        const auto patched =
            apply_tile_map_projection_page(maximum, defs, tile_map_projection_page(changed, delta));
        equal(patched, changed);
        check(pointers(patched).at({1, {1, 0}}) == pointers(maximum).at({1, {1, 0}}),
              "Delta copied an unchanged chunk array");
        check(tile_map_projection_changes(changed.with_cells(edit, defs), &changed).empty(),
              "No-op edit generated changed chunks");
        std::vector<TileMapCellEdit> moved;
        for (int slot = 0; slot < 256; ++slot) {
            moved.push_back({1, {1023 * 32 + slot % 32, slot / 32}, {}});
            moved.push_back({1, {-32 + slot % 32, slot / 32}, {wide, 7}});
        }
        const auto relocated = maximum.with_cells(moved, defs);
        const auto move_keys = tile_map_projection_changes(relocated, &maximum);
        check(move_keys.size() == 2 && move_keys[0].address.x == 1023 &&
                  move_keys[1].address.x == -1,
              "Capacity delta did not remove before adding");
        auto moving = maximum;
        for (const auto key : move_keys)
            moving = apply_tile_map_projection_page(
                moving, defs, tile_map_projection_page(relocated, std::span(&key, 1)));
        equal(moving, relocated);
        const std::array<TileMapCellEdit, 1> far{{{1, {INT32_MIN, INT32_MAX}, {wide, 7}}}};
        const auto extreme = header.empty.with_cells(far, defs);
        equal(apply_tile_map_projection_page(
                  header.empty, defs,
                  tile_map_projection_page(extreme, tile_map_projection_changes(extreme))),
              extreme);
        const auto dense = profile(id, set, 4, 1024, true);
        const auto dense_page = tile_map_projection_page(dense, tile_map_projection_changes(dense));
        check(dense_page.dump().size() < tile_projection_page_bytes,
              "Wide uint64 page exceeds profile");
        equal(apply_tile_map_projection_page(header.empty, defs, dense_page), dense);
        rejects([&] {
            (void)tile_map_projection_changes(profile(AssetId::generate(), set, 0, 0), &maximum);
        });
        rejects([&] {
            (void)decode_tile_map_projection_header(tile_map_projection_header(maximum, defs),
                                                    AssetId::generate(), set);
        });
        auto bad_header = tile_map_projection_header(maximum, defs);
        bad_header["map"]["cell_count"] = 0;
        rejects([&] { (void)decode_tile_map_projection_header(bad_header, id, set); });
        const auto good = tile_map_projection_page(changed, delta);
        const auto baseline = pointers(maximum);
        std::vector<Json> malformed;
        auto bad = good;
        bad.push_back(bad[0]);
        malformed.push_back(bad);
        bad = good;
        bad[0]["x"] = UINT64_MAX;
        malformed.push_back(bad);
        bad = good;
        bad[0]["x"] = 1.25;
        malformed.push_back(bad);
        bad = good;
        bad[0]["x"] = INT32_MAX;
        malformed.push_back(bad);
        bad = good;
        bad[0]["layer"] = 99;
        malformed.push_back(bad);
        bad = good;
        bad[0]["cells"][1][0] = 0;
        malformed.push_back(bad);
        bad = good;
        bad[0]["cells"][0][1] = 0;
        malformed.push_back(bad);
        bad = good;
        bad[0]["cells"][0][1] = 1234;
        malformed.push_back(bad);
        bad = good;
        bad[0]["cells"][0][2] = 8;
        malformed.push_back(bad);
        bad = good;
        bad[0]["cells"].push_back({1024, 9, 0});
        malformed.push_back(bad);
        bad = good;
        bad[0]["opaque"] = true;
        malformed.push_back(bad);
        bad = Json::array();
        for (int n = 0; n < 5; ++n)
            bad.push_back(good[0]);
        malformed.push_back(bad);
        for (const auto& page : malformed)
            rejects([&] { (void)apply_tile_map_projection_page(maximum, defs, page); });
        check(pointers(maximum) == baseline, "Rejected packet changed the admitted baseline");
        TileMapPlaySource source;
        const auto actor = EntityId::generate();
        const auto scene = AssetId::generate();
        TileMapPlayView view;
        view.entity = actor;
        view.requested = id;
        view.generation = 1;
        view.map_revision = "map-one";
        view.set_revision = "set-one";
        view.cells = std::make_shared<const TileMapData>(maximum);
        view.definitions = std::make_shared<const TileSetData>(defs);
        const auto manifest = source.capture(1, scene, std::span(&view, 1));
        check(manifest.at("maps")[0].at("cells") == 262144 &&
                  !manifest.at("maps")[0].contains("header") && manifest.dump().size() < 2048,
              "Normal Play observation inlined cell arrays");
        auto begin = source.begin(actor, 1, 0, 0, 0);
        auto token = begin.at("token").get<std::uint64_t>();
        check(begin.at("full") == true && begin.at("count") == 1024 && !source.acknowledge(token),
              "Initial transfer accepted an incomplete acknowledgement");
        rejects([&] { (void)source.page(token, 1); });
        std::size_t cursor = 0;
        while (cursor < 1024) {
            const auto packet = source.page(token, cursor);
            check(packet.at("cursor") == cursor && packet.at("token") == token,
                  "Play page lost its correlation");
            cursor = packet.at("next").get<std::size_t>();
        }
        check(source.acknowledge(token) && source.acknowledge(token),
              "Completed transfer did not retain its idempotent baseline");
        auto independent = view;
        independent.entity = EntityId::generate();
        independent.generation = 2;
        independent.revision = 1;
        independent.edited = true;
        independent.cells = std::make_shared<const TileMapData>(changed);
        const std::array shared_actors{view, independent};
        source.capture(1, scene, shared_actors);
        const auto shared_begin = source.begin(independent.entity, 2, 1, 2, 0);
        check(shared_begin.at("full") == false && shared_begin.at("count") == 1,
              "First edit of another actor retransmitted a known identical initial asset");
        const auto shared_token = shared_begin.at("token").get<std::uint64_t>();
        equal(
            apply_tile_map_projection_page(maximum, defs, source.page(shared_token, 0).at("data")),
            changed);
        check(source.acknowledge(shared_token), "Shared initial baseline did not acknowledge");
        view.cells = std::make_shared<const TileMapData>(changed);
        view.revision = 1;
        view.edited = true;
        source.capture(1, scene, std::span(&view, 1));
        begin = source.begin(actor, 1, 1, 1, 0);
        const auto delta_token = begin.at("token").get<std::uint64_t>();
        check(begin.at("full") == false && begin.at("count") == 1 && !begin.contains("header") &&
                  source.acknowledge(shared_token) && !source.acknowledge(token),
              "Acknowledged one-cell edit retransmitted the map or rejected a repeated old ack");
        // A newer observation cannot alter the already frozen value/pages.
        view.cells = std::make_shared<const TileMapData>(relocated);
        view.revision = 2;
        source.capture(1, scene, std::span(&view, 1));
        const auto frozen = source.page(delta_token, 0);
        equal(apply_tile_map_projection_page(maximum, defs, frozen.at("data")), changed);
        check(source.acknowledge(delta_token) && source.retained_bytes() < 64 * 1024 * 1024,
              "Frozen delta baseline or retained budget failed");
        begin = source.begin(actor, 1, 2, 1, 1);
        token = begin.at("token").get<std::uint64_t>();
        check(source.cancel(token) && !source.cancel(token) && !source.acknowledge(token),
              "Cancellation accepted stale transfer operations");
        source.capture(1, scene, {});
        check(source.retained_bytes() == 0 && source.begin(actor, 1, 2, 0, 0).at("stale") == true,
              "Departed actor retained copied state or remained fetchable");
        source.capture(2, scene, std::span(&view, 1));
        check(!source.acknowledge(delta_token), "Old-world acknowledgement crossed scope reset");
        auto partial = view;
        partial.definitions.reset();
        rejects([&] { (void)source.capture(3, scene, std::span(&partial, 1)); });
        check(source.world() == 2 && source.scene() == scene,
              "Rejected observation changed the transport scope");
        bool foreign = false;
        std::thread other([&] {
            try {
                source.clear();
            } catch (const std::exception&) {
                foreign = true;
            }
        });
        other.join();
        check(foreign && source.world() == 2, "Foreign thread mutated Play projection state");
        // Empty maps exercise baseline eviction without a second large workload.
        std::vector<TileMapPlayView> many(17, view);
        for (auto& item : many) {
            item.entity = EntityId::generate();
            item.cells = std::make_shared<const TileMapData>(header.empty);
        }
        source.capture(4, scene, many);
        for (const auto& item : many) {
            begin = source.begin(item.entity, 1, 2, 0, 0);
            check(begin.at("count") == 0 && source.acknowledge(begin.at("token")),
                  "Empty map transfer required cell data");
        }
        begin = source.begin(many.front().entity, 1, 2, 1, 2);
        check(begin.at("full") == true, "Bounded baseline cache retained every old actor");
        source.clear();
        check(source.retained_bytes() == 0, "Clear retained copied cell history");
        std::cout << "Bounded tile presentation headers/full pages/deltas/capacity moves/extreme "
                     "keys/rejections and frozen acknowledged source transfers passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
