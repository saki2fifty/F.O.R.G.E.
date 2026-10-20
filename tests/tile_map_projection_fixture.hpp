#pragma once
#include "cooked_envelope.hpp"
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
inline TileSetData definitions(AssetId id) {
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
inline TileMapData profile(AssetId id, AssetId set, unsigned count, unsigned occupied,
                           bool wide_keys = false, unsigned flags = 0) {
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
    return decode_cooked_tile_map(asset_detail::encode_envelope(root, payload,
                                                                tile_map_cooked_magic,
                                                                tile_map_cooked_metadata_limit),
                                  id);
}
inline auto pointers(const TileMapData& map) {
    std::map<TileMapChunkKey, const TileMapCell*> out;
    map.for_each_chunk([&](auto key, auto cells, auto) { out[key] = cells.data(); });
    return out;
}
inline void equal(const TileMapData& actual, const TileMapData& expected) {
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
} // namespace forge::test_tile_projection
