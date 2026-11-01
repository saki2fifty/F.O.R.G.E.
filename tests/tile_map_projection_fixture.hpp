#pragma once
#include "cooked_envelope.hpp"
#include "tile_map_archive.hpp"
#include "tile_map_projection.hpp"
#include <algorithm>
#include <stdexcept>

namespace forge::test_tile_projection {
using Json = nlohmann::json;
inline constexpr auto wide = UINT64_MAX - 1;
inline void check(bool ok, const char* why) {
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
inline TileSetData definitions(AssetId id, bool collision_profiles = false,
                               bool concave_profile = false, bool animated = false,
                               bool neighbors = false) {
    collision_profiles |= concave_profile || animated || neighbors;
    Json root{{"asset_id", id},
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
    if (collision_profiles) {
        root["tiles"][0]["collision"] = {{"kind", "convex"},
                                         {"points", {{0., 0.}, {1., 0.}, {1., 1.}}}};
        root["tiles"][1]["collision"] = {{"kind", "none"}};
    }
    if (concave_profile)
        root["tiles"][0]["collision"] = {
            {"kind", "polygon"},
            {"points", {{0., 0.}, {1., 0.}, {1., .5}, {.5, .5}, {.5, 1.}, {0., 1.}}}};
    if (animated)
        root["tiles"][0]["animation"] = {
            {"enabled", true},
            {"frames", {{{"frame", 0}, {"seconds", .125}}, {{"frame", 1}, {"seconds", .125}}}}};
    if (neighbors)
        root["tiles"][0]["autotile"] = {{"enabled", true},
                                        {"rules", {{{"mask", 0}, {"same", 0}, {"tile", wide}}}}};
    return decode_cooked_tile_set(asset_detail::encode_envelope(root, {}, tile_set_cooked_magic,
                                                                tile_set_cooked_metadata_limit,
                                                                neighbors            ? 5
                                                                : animated           ? 4
                                                                : concave_profile    ? 3
                                                                : collision_profiles ? 2
                                                                                     : 1),
                                  id);
}
inline TileMapData profile(AssetId id, AssetId set, unsigned count, unsigned occupied,
                           bool wide_keys = false, unsigned flags = 0, bool compact = false) {
    std::vector<std::byte> payload;
    for (unsigned chunk = 0; chunk < count; ++chunk) {
        for (auto scalar : {1u, 0u, chunk, 0u, occupied})
            asset_detail::cooked_write32(payload, scalar);
        for (unsigned slot = 0; slot < occupied; ++slot) {
            const auto key = wide_keys ? wide : 9;
            for (auto scalar : {slot, std::uint32_t(key), std::uint32_t(key >> 32), flags})
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
    const auto bytes = asset_detail::encode_envelope(root, payload, tile_map_cooked_magic,
                                                     tile_map_cooked_metadata_limit);
    return compact ? decode_tile_map_archive(bytes, id).compact()
                   : decode_cooked_tile_map(bytes, id);
}
inline auto storage_identities(const TileMapData& map) {
    std::map<TileMapChunkKey, const void*> out;
    map.for_each_chunk_info([&](auto key, auto, const void* token) { out[key] = token; });
    return out;
}
inline void equal(const TileMapData& actual, const TileMapData& expected) {
    check(actual.asset() == expected.asset() && actual.tile_set() == expected.tile_set() &&
              actual.cell_size() == expected.cell_size() && actual.layers() == expected.layers() &&
              actual.occupied() == expected.occupied() &&
              actual.chunk_count() == expected.chunk_count(),
          "Copied tile metadata/counts differ");
    actual.for_each_chunk_values([&](auto key, auto cells, auto count) {
        check(expected.visit_chunk_values(key,
                                          [&](auto other, auto occupied) {
                                              check(count == occupied &&
                                                        std::equal(cells.begin(), cells.end(),
                                                                   other.begin()),
                                                    "Copied chunk contents differ");
                                          }),
              "Copied chunk key is absent from the expected logical map");
    });
}
} // namespace forge::test_tile_projection
