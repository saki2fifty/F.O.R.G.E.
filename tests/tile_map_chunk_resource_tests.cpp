#include "tile_asset_resources.hpp"
#include "tile_map_cook.hpp"
#include "tile_map_projection_fixture.hpp"
#include <chrono>
#include <iostream>
#include <thread>

using namespace forge;
using namespace std::chrono_literals;
namespace {
void check(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void reject(F&& action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Expected Tile Map chunk resource rejection");
}
TileMapArchive archive(AssetId map, AssetId set, std::uint64_t tile) {
    auto source = TileMapValue::create(map, set);
    const std::array<TileMapCellEdit, 2> writes{{{1, {}, {tile, 3}}, {1, {-1, -1}, {tile, 7}}}};
    return decode_tile_map_archive(cook_tile_map(source.paint(writes)), map);
}
ResourceLease<TileMapArchiveResource> adopt(ResourcePool<TileMapArchiveResource>& pool,
                                            TileMapArchive data, std::uint64_t generation) {
    const auto id = data.asset();
    const auto revision =
        asset_build_digest({{"fixture", generation}, {"tile", data.cell(1, {}).tile}});
    auto ticket = pool.request(
        {id}, revision, generation,
        [data = std::move(data)](std::stop_token stop) {
            check(!stop.stop_requested(), "Fixture archive cancelled");
            const auto bytes = data.resident_bytes();
            return ResourceCandidate<TileMapArchiveResource>{std::make_unique<TileMapArchive>(data),
                                                             ResourceMemory{bytes}};
        },
        {}, 0, "tile-map:archive");
    check(pool.wait(ticket, 5s), "Fixture archive adoption failed: " + ticket.inspect().diagnostic);
    return pool.acquire(ticket);
}
void drain(ResourcePool<TileMapChunkResource>& pool) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (pool.statistics().pending) {
        pool.pump();
        check(std::chrono::steady_clock::now() < deadline, "Chunk cancellation did not drain");
        std::this_thread::sleep_for(1ms);
    }
}
void chunk_lifetime_and_cancel() {
    const auto map = AssetId::generate(), set = AssetId::generate();
    ResourcePool<TileMapArchiveResource> sources({1, 4, 8, 1024 * 1024});
    ResourcePool<TileMapChunkResource> chunks({1, 8, 16, 8 * sizeof(TileMapChunkData)});
    auto source = adopt(sources, archive(map, set, UINT64_MAX), 1);
    const TileMapChunkKey first{1, {0, 0}}, second{1, {-1, -1}};
    const auto ticket = request_tile_map_chunk(chunks, source, first);
    const auto duplicate = request_tile_map_chunk(chunks, source, first);
    check(ticket.inspect().identity == duplicate.inspect().identity,
          "Equal chunk requests did not coalesce");
    check(chunks.wait(ticket, 5s), "Requested dense chunk failed: " + ticket.inspect().diagnostic);
    auto held = chunks.acquire(ticket);
    check(held && held->occupied == 1 && held->cells[0] == TileMapCell{UINT64_MAX, 3} &&
              held.identity().asset == map &&
              chunks.statistics().memory.total() == sizeof(TileMapChunkData),
          "Chunk load changed cells/root identity or charged complete-map residency");
    const auto missing_count = chunks.pending_requests().size();
    reject([&] { (void)request_tile_map_chunk(chunks, source, {1, {10, 10}}); });
    check(chunks.pending_requests().size() == missing_count,
          "Invalid chunk allocated a request slot");
    const auto cancelled = request_tile_map_chunk(chunks, source, second);
    const auto variant = cancelled.inspect().identity.variant;
    chunks.unload({map}, variant);
    drain(chunks);
    check(!chunks.current({map}, variant) && !chunks.acquire(cancelled),
          "A cancelled/stale chunk completion reactivated an unloaded selection");
    const auto pending = request_tile_map_chunk(chunks, source, second);
    check(pending.inspect().identity != cancelled.inspect().identity &&
              pending.inspect().identity.variant != ticket.inspect().identity.variant,
          "Re-request reused a cancelled identity or another chunk's variant");
    // Source close revokes leases while the chunk worker still has its own pure
    // immutable input. No filesystem or pointer into the revoked scope is used.
    sources.close();
    reject([&] { (void)source.get(); });
    check(chunks.wait(pending, 5s), "Detached archive backing did not survive source close");
    auto other = chunks.acquire(pending);
    check(other && other->key == second && other->cells[1023] == TileMapCell{UINT64_MAX, 7},
          "Independent chunk decode lost its owned backing or signed slot");
    chunks.unload({map}, ticket.inspect().identity.variant);
    check(!chunks.acquire(ticket) && held->cells[0] == TileMapCell{UINT64_MAX, 3},
          "Unloading a chunk retargeted or prematurely retired a strong lease");
    check(chunks.statistics().retired == 1, "Held unloaded chunk is not charged as retiring");
    held = {};
    chunks.collect();
    check(chunks.statistics().retired == 0, "Released dense chunk did not retire");
    chunks.close();
    reject([&] { (void)other.get(); });
}
void replacement_budget_and_retry() {
    const auto map = AssetId::generate(), set = AssetId::generate();
    ResourcePool<TileMapArchiveResource> sources({1, 4, 8, 1024 * 1024});
    ResourcePool<TileMapChunkResource> chunks({1, 4, 8, sizeof(TileMapChunkData)});
    auto first = adopt(sources, archive(map, set, 17), 1);
    const TileMapChunkKey key{1, {0, 0}};
    const auto before = request_tile_map_chunk(chunks, first, key);
    check(chunks.wait(before, 5s), "Baseline chunk did not fit one-page budget");
    auto pinned = chunks.acquire(before);
    auto second = adopt(sources, archive(map, set, 99), 2);
    const auto changed = request_tile_map_chunk(chunks, second, key);
    check(!chunks.wait(changed, 5s) && changed.inspect().state == ResourceState::Failed &&
              changed.inspect().previous_good && pinned->cells[0].tile == 17 &&
              chunks.current({map}, changed.inspect().identity.variant)->cells[0].tile == 17 &&
              chunks.statistics().memory.total() == sizeof(TileMapChunkData),
          "Failed replacement lost last-good data or exceeded its one-page budget");
    reject([&] { (void)request_tile_map_chunk(chunks, first, key); });
    const auto variant = changed.inspect().identity.variant;
    chunks.unload({map}, variant);
    pinned = {};
    chunks.collect();
    check(chunks.statistics().memory.total() == 0,
          "Unloaded page remained resident without a lease");
    const auto retry = request_tile_map_chunk(chunks, second, key);
    check(chunks.wait(retry, 5s) && chunks.acquire(retry)->cells[0].tile == 99,
          "New revision did not retry after reclaiming the old dense page");
}
void independent_source_scopes() {
    const auto map = AssetId::generate(), set = AssetId::generate();
    ResourcePool<TileMapArchiveResource> first_source({1, 4, 8, 1024 * 1024});
    ResourcePool<TileMapArchiveResource> second_source({1, 4, 8, 1024 * 1024});
    ResourcePool<TileMapChunkResource> chunks({1, 4, 8, 4 * sizeof(TileMapChunkData)});
    const auto first = adopt(first_source, archive(map, set, 17), 1);
    const auto second = adopt(second_source, archive(map, set, 99), 1);
    check(first.identity().generation == second.identity().generation &&
              first.identity().owner != second.identity().owner,
          "Source-scope fixture did not isolate independent generation domains");
    const TileMapChunkKey key{1, {0, 0}};
    const auto a = request_tile_map_chunk(chunks, first, key);
    const auto b = request_tile_map_chunk(chunks, second, key);
    check(a.inspect().identity.asset == b.inspect().identity.asset &&
              a.inspect().identity.variant != b.inspect().identity.variant && chunks.wait(a, 5s) &&
              chunks.wait(b, 5s),
          "Independent source-owner generations conflicted in one chunk pool");
    check(chunks.acquire(a)->cells[0].tile == 17 && chunks.acquire(b)->cells[0].tile == 99,
          "Source scope isolation returned another owner's chunk data");
}
void immutable_value_inputs() {
    const auto map = AssetId::generate(), set = AssetId::generate();
    const auto definitions = test_tile_projection::definitions(set);
    auto backing = archive(map, set, 9);
    auto sparse = backing.compact(), dense = backing.materialize();
    const TileMapChunkKey key{1, {0, 0}}, neighbor{1, {-1, -1}};
    auto encoded = std::make_shared<const TileMapChunkInput>(sparse.chunk_input(key));
    auto full = std::make_shared<const TileMapChunkInput>(dense.chunk_input(key));
    auto other = sparse.chunk_input(neighbor);
    check(encoded->asset() == map && encoded->key() == key && encoded->occupied() == 1 &&
              encoded->encoded_cell_records()->size() == 16 && !full->encoded_cell_records() &&
              encoded->backing_identity() == other.backing_identity() &&
              encoded->backing_bytes() > 16 && encoded->input_bytes() > sizeof(TileMapChunkInput),
          "Immutable chunk input lost identity, representation or shared-backing accounting");
    const std::array<TileMapCellEdit, 1> edit{{{1, {}, {test_tile_projection::wide, 6}}}};
    auto edited = sparse.with_cells(edit, definitions);
    auto changed = std::make_shared<const TileMapChunkInput>(edited.chunk_input(key));
    check(encoded->identity() != changed->identity() && !changed->encoded_cell_records(),
          "Edited chunk input retained the old storage or failed to use its dense COW value");
    std::stop_source stop;
    stop.request_stop();
    reject([&] { (void)encoded->decode(stop.get_token()); });
    reject([&] { (void)full->decode(stop.get_token()); });
    reject([&] { (void)sparse.chunk_input({1, {2, 2}}); });
    reject([] { (void)TileMapChunkInput{}.decode(); });
    ResourcePool<TileMapChunkResource> pages({1, 8, 16, 4 * sizeof(TileMapChunkData)});
    const auto original = request_tile_map_chunk_values(pages, encoded);
    const auto duplicate = request_tile_map_chunk_values(pages, full);
    const auto replacement = request_tile_map_chunk_values(pages, changed);
    check(original.inspect().identity == duplicate.inspect().identity &&
              original.inspect().identity.variant != replacement.inspect().identity.variant,
          "Chunk content identity changed with storage representation or ignored edited cells");
    const std::weak_ptr<const TileMapChunkInput> pending_input = encoded;
    encoded.reset();
    full.reset();
    changed.reset();
    other = {};
    backing = {};
    sparse = {};
    dense = {};
    edited = {};
    check(pages.wait(original, 5s) && pages.wait(replacement, 5s),
          "Pure chunk inputs did not survive retirement of their source values");
    const auto old = pages.acquire(original), current = pages.acquire(replacement);
    check(old->cells[0] == TileMapCell{9, 3} &&
              current->cells[0] == TileMapCell{test_tile_projection::wide, 6} &&
              pending_input.expired() && pages.statistics().selected == 2 &&
              pages.statistics().memory.total() == 2 * sizeof(TileMapChunkData),
          "Content pages retargeted a prior selection or retained source input backing");
}
} // namespace
int main() {
    try {
        chunk_lifetime_and_cancel();
        replacement_budget_and_retry();
        independent_source_scopes();
        immutable_value_inputs();
        std::cout << "Async Tile Map chunk variants, cancellation, source retirement, budget and "
                     "last-good retry passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
