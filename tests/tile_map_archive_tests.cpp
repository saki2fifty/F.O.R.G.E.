#include "tile_map_archive.hpp"
#include "tile_map_cook.hpp"
#include <algorithm>
#include <iostream>
#include <set>

using namespace forge;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void reject(F&& action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Expected Tile Map archive rejection");
}
TileMapValue example() {
    auto source = TileMapValue::create(AssetId::generate(), AssetId::generate());
    auto metadata = source.metadata();
    metadata["cell_size"] = {.25, 2.};
    metadata["next_id"] = 3;
    metadata["layers"].push_back(
        {{"id", 2}, {"name", "Hidden cells"}, {"visible", false}, {"order", -7}});
    source = source.with_metadata(std::move(metadata));
    const std::array<TileMapCellEdit, 8> writes{{{1, {0, 0}, {1, 0}},
                                                 {1, {2, 0}, {UINT64_MAX, 7}},
                                                 {1, {31, 31}, {17, 3}},
                                                 {1, {-1, -1}, {0x0102030405060708, 4}},
                                                 {1, {-32, -32}, {33, 1}},
                                                 {1, {INT32_MIN, INT32_MIN}, {99, 2}},
                                                 {1, {INT32_MAX, INT32_MAX}, {101, 5}},
                                                 {2, {0, 0}, {47, 6}}}};
    return source.paint(writes);
}
void owned_index_and_cells() {
    const auto source = example();
    auto bytes = cook_tile_map(source);
    auto archive = decode_tile_map_archive(bytes, source.asset());
    check(archive.asset() == source.asset() && archive.tile_set() == source.tile_set() &&
              archive.cell_size() == source.cell_size() && archive.layers() == source.layers() &&
              archive.occupied() == 8 && archive.chunks().size() == 5,
          "Archive metadata or sparse chunk index differs from authored values");
    std::fill(bytes.begin(), bytes.end(), std::byte{255});
    bytes.clear();
    bytes.shrink_to_fit();
    const auto full = archive.materialize();
    std::set<TileMapChunkKey> visited;
    std::size_t total = 0;
    for (const auto& info : archive.chunks()) {
        check(visited.insert(info.key).second, "Archive index repeats a chunk");
        const auto chunk = archive.decode_chunk(info.key);
        check(chunk.key == info.key && chunk.occupied == info.occupied,
              "Requested chunk has different identity or occupancy");
        std::size_t occupied = 0;
        for (unsigned slot = 0; slot < tile_chunk_cells; ++slot) {
            const auto at = tile_cell_address({info.key.address, slot});
            const auto expected = source.cell(info.key.layer, at);
            check(chunk.cells[slot] == expected && archive.cell(info.key.layer, at) == expected &&
                      full.cell(info.key.layer, at) == expected,
                  "Direct sparse lookup or isolated dense decode differs from source cells");
            occupied += bool(expected.tile);
        }
        check(occupied == info.occupied, "Archive occupancy differs from decoded occupied cells");
        total += occupied;
    }
    check(total == 8 && archive.cell(1, {64, 64}) == TileMapCell{} &&
              archive.cell(1, {1, 0}) == TileMapCell{},
          "Archive changed absent-chunk or empty-slot lookup");
    // A detached archive owns its backing independently of the original value.
    auto detached = archive;
    archive = {};
    check(detached.cell(1, {2, 0}) == TileMapCell{UINT64_MAX, 7},
          "Archive copy lost backing when its original owner retired");
    const auto before = detached.resident_bytes();
    (void)detached.decode_chunk({1, {0, 0}});
    (void)detached.materialize();
    check(detached.resident_bytes() == before,
          "Read-only chunk decoding installed hidden mutable dense residency");
    reject([&] { archive.validate(); });
    reject([&] { (void)detached.cell(3, {}); });
    reject([&] { (void)detached.decode_chunk({1, {2, 2}}); });
    reject([&] { (void)decode_tile_map_archive(cook_tile_map(source), AssetId::generate()); });
    auto moved = std::move(detached);
    moved.validate();
    reject([&] { detached.validate(); });
    std::stop_source cancelled;
    cancelled.request_stop();
    reject([&] { (void)moved.decode_chunk({1, {0, 0}}, cancelled.get_token()); });
    reject([&] { (void)moved.materialize(cancelled.get_token()); });
    check(moved.decode_chunk({1, {0, 0}}).occupied == 3,
          "A cancelled decode poisoned subsequent independent decoding");
}
void empty_and_bad_bytes() {
    const auto source = TileMapValue::create(AssetId::generate(), AssetId::generate());
    auto bytes = cook_tile_map(source);
    const auto empty = decode_tile_map_archive(bytes, source.asset());
    check(empty.chunks().empty() && !empty.occupied() && empty.cell(1, {}) == TileMapCell{} &&
              !empty.materialize().chunk_count(),
          "Empty admitted archive differs from the complete empty map");
    std::stop_source cancelled;
    cancelled.request_stop();
    reject([&] { (void)empty.materialize(cancelled.get_token()); });
    bytes[0] = std::byte{};
    reject([&] { (void)decode_tile_map_archive(bytes, source.asset()); });
    reject([] { TileMapArchive{}.validate(); });
}
} // namespace
int main() {
    try {
        owned_index_and_cells();
        empty_and_bad_bytes();
        std::cout << "Owned sparse Tile Map archive, isolated decoding, exact queries and "
                     "cancellation passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
