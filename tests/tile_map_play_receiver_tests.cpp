#include "tile_map_play_receiver.hpp"
#include "tile_map_projection_fixture.hpp"
#include <forge/render_scene.hpp>
#include <iostream>
using namespace forge;
using namespace forge::test_tile_projection;
namespace {
Json respond(TileMapPlaySource& source, const Json& request) {
    if (request.at("world") != source.world() ||
        request.at("scene").get<AssetId>() != source.scene())
        return {{"stale", true}};
    const auto command = request.at("command").get<std::string>();
    if (command == "tile_map_begin")
        return source.begin(request.at("entity"), request.at("generation"), request.at("revision"),
                            request.at("base_generation"), request.at("base_revision"));
    if (command == "tile_map_page")
        return source.page(request.at("token"), request.at("cursor"));
    if (command == "tile_map_ack")
        return {{"accepted", source.acknowledge(request.at("token"))}};
    if (command == "tile_map_cancel")
        return {{"accepted", source.cancel(request.at("token"))}};
    throw std::runtime_error("Unknown copied tile request");
}
RenderTileMap bound(TileMapPlayReceiver& receiver, AssetId scene, const TileMapPlayView& view) {
    RenderScene frame;
    frame.scene = scene;
    RenderTileMap map;
    map.entity = view.entity;
    map.renderer.map.id = view.requested;
    // Deliberately fill authoring-looking data; bind must clear it until an
    // admitted runtime value is complete, rather than falling back to it.
    map.cells = view.cells;
    map.definitions = view.definitions;
    frame.tile_maps.push_back(map);
    receiver.bind(frame);
    return frame.tile_maps.front();
}
void step(TileMapPlaySource& source, TileMapPlayReceiver& receiver) {
    const auto request = receiver.next_request();
    check(request.has_value(), "Receiver made no bounded progress");
    check(!receiver.next_request(), "Receiver reserved two simultaneous requests");
    check(receiver.receive(respond(source, *request)), "Receiver ignored its correlated response");
}
void drain(TileMapPlaySource& source, TileMapPlayReceiver& receiver) {
    for (unsigned attempt = 0; receiver.pending(); ++attempt) {
        check(attempt < 2048, "Copied tile roundtrip did not complete");
        step(source, receiver);
    }
}
} // namespace
int main() {
    try {
        const auto id = AssetId::generate(), set = AssetId::generate(), scene = AssetId::generate();
        const auto defs = definitions(set);
        const auto original = profile(id, set, 7, 4);
        TileMapPlayView view;
        view.entity = EntityId::generate();
        view.requested = id;
        view.generation = 1;
        view.map_revision = "map-one";
        view.set_revision = "set-one";
        view.cells = std::make_shared<const TileMapData>(original);
        view.definitions = std::make_shared<const TileSetData>(defs);
        TileMapPlaySource source;
        TileMapPlayReceiver receiver;
        receiver.observe(source.capture(1, scene, std::span(&view, 1)));
        auto frame = bound(receiver, scene, view);
        check(frame.runtime_instance && frame.pending && !frame.cells && !frame.definitions,
              "Game binding used authored cells before copied admission");
        step(source, receiver); // Begin.
        step(source, receiver); // First four chunks.
        check(!bound(receiver, scene, view).cells, "Partial transfer published a runtime frame");
        step(source, receiver); // Remaining chunks.
        check(!bound(receiver, scene, view).cells,
              "Unacknowledged transfer published a runtime frame");
        step(source, receiver); // Ack.
        frame = bound(receiver, scene, view);
        check(frame.cells && !frame.pending && frame.revision == 0,
              "Acknowledged value was not bound");
        equal(*frame.cells, original);
        const auto retained = frame.cells;
        check(!receiver.pending() && !receiver.next_request(),
              "Unchanged runtime values retransmitted cells");
        auto another = view;
        another.entity = EntityId::generate();
        another.generation = 2;
        const std::array shared{view, another};
        receiver.observe(source.capture(1, scene, shared));
        const auto second = bound(receiver, scene, another);
        check(second.cells == retained && !receiver.next_request(),
              "Known identical initial asset did not share its copied arrays");
        const std::array<TileMapCellEdit, 1> writes{{{1, {0, 0}, {wide, 5}}}};
        auto changed = original.with_cells(writes, defs);
        view.cells = std::make_shared<const TileMapData>(changed);
        view.revision = 1;
        view.edited = true;
        receiver.observe(source.capture(1, scene, std::span(&view, 1)));
        check(bound(receiver, scene, view).cells == retained && receiver.pending(),
              "Update discarded the last complete copied frame");
        step(source, receiver); // Freeze revision1.
        const std::array<TileMapCellEdit, 1> later_write{{{1, {32, 0}, {9, 3}}}};
        const auto later = changed.with_cells(later_write, defs);
        view.cells = std::make_shared<const TileMapData>(later);
        view.revision = 2;
        receiver.observe(source.capture(1, scene, std::span(&view, 1)));
        step(source, receiver);
        step(source, receiver); // Complete frozen1 then ack.
        frame = bound(receiver, scene, view);
        check(frame.revision == 1 && receiver.pending(),
              "New observation mixed into frozen revision1");
        equal(*frame.cells, changed);
        drain(source, receiver);
        frame = bound(receiver, scene, view);
        equal(*frame.cells, later);
        check(frame.revision == 2 && retained->cell(1, {}).tile == 9,
              "New frame mutated an older captured value");
        auto wrong_scene = bound(receiver, AssetId::generate(), view);
        check(wrong_scene.runtime_instance && !wrong_scene.cells && wrong_scene.pending,
              "Copied cells crossed Scene scope");
        // Malformed page: keep current frame, cancel sender, then recover normally.
        const std::array<TileMapCellEdit, 1> third_write{{{1, {0, 0}, {9, 6}}}};
        const auto new_data = later.with_cells(third_write, defs);
        view.cells = std::make_shared<const TileMapData>(new_data);
        view.revision = 3;
        receiver.observe(source.capture(1, scene, std::span(&view, 1)));
        step(source, receiver);
        auto request = receiver.next_request();
        check(request && request->at("command") == "tile_map_page", "Expected delta page");
        auto response = respond(source, *request);
        response["token"] = 0;
        rejects([&] { receiver.receive(response); });
        check(bound(receiver, scene, view).revision == 2,
              "Bad packet changed the last complete runtime frame");
        step(source, receiver); // Explicit cancel releases frozen source.
        receiver.observe(source.capture(1, scene, std::span(&view, 1)));
        drain(source, receiver);
        check(bound(receiver, scene, view).revision == 3 && receiver.diagnostic().empty(),
              "Valid retry did not recover after malformed packet");
        const auto before_noop = bound(receiver, scene, view).cells;
        view.revision = 4; // A valid runtime no-op request still has a new revision.
        receiver.observe(source.capture(1, scene, std::span(&view, 1)));
        step(source, receiver);
        auto no_op_ack = receiver.next_request();
        check(no_op_ack && no_op_ack->at("command") == "tile_map_ack",
              "Unchanged chunks required a cell page");
        receiver.receive(respond(source, *no_op_ack));
        check(bound(receiver, scene, view).cells == before_noop &&
                  bound(receiver, scene, view).revision == 4,
              "No-op revision rebuilt the numeric render view");
        // New instance invalidates staged old cells even at the same entity/root.
        view.cells = std::make_shared<const TileMapData>(changed);
        view.revision = 5;
        receiver.observe(source.capture(1, scene, std::span(&view, 1)));
        step(source, receiver);
        view.cells = std::make_shared<const TileMapData>(original);
        view.generation = 3;
        view.revision = 0;
        view.edited = false;
        receiver.observe(source.capture(1, scene, std::span(&view, 1)));
        check(!bound(receiver, scene, view).cells, "New instance retained old edited cells");
        step(source, receiver);
        drain(source, receiver);
        equal(*bound(receiver, scene, view).cells, original);
        // Scope replacement can arrive while a begin reply is still in flight.
        view.revision = 1;
        view.edited = true;
        view.cells = std::make_shared<const TileMapData>(changed);
        receiver.observe(source.capture(1, scene, std::span(&view, 1)));
        request = receiver.next_request();
        response = respond(source, *request);
        receiver.observe(source.capture(2, scene, std::span(&view, 1)));
        check(!receiver.receive(response) && !bound(receiver, scene, view).cells,
              "Old-world begin reply crossed replacement");
        drain(source, receiver);
        equal(*bound(receiver, scene, view).cells, changed);
        // Header failure still knows the sender token and must cancel it.
        receiver.clear();
        receiver.observe(source.capture(3, scene, std::span(&view, 1)));
        request = receiver.next_request();
        response = respond(source, *request);
        response["header"]["map"]["asset_id"] = AssetId::generate();
        rejects([&] { receiver.receive(response); });
        request = receiver.next_request();
        check(request && request->at("command") == "tile_map_cancel",
              "Invalid header stranded a frozen sender");
        receiver.receive(respond(source, *request));
        receiver.observe(source.capture(3, scene, std::span(&view, 1)));
        drain(source, receiver);
        // A full fallback starts from received storage, but removes old chunks
        // absent from the complete manifest before admitting new chunks.
        const auto old_full = bound(receiver, scene, view).cells;
        std::vector<TileMapCellEdit> move_chunk;
        for (int slot = 0; slot < 4; ++slot) {
            move_chunk.push_back({1, {6 * 32 + slot, 0}, {}});
            move_chunk.push_back({1, {-32 + slot, 0}, {wide, 4}});
        }
        view.cells = std::make_shared<const TileMapData>(view.cells->with_cells(move_chunk, defs));
        ++view.revision;
        source.clear(); // Model eviction of all transport baselines, not a world reset.
        receiver.observe(source.capture(3, scene, std::span(&view, 1)));
        drain(source, receiver);
        const auto full = bound(receiver, scene, view).cells;
        equal(*full, *view.cells);
        check(!full->cell(1, {6 * 32, 0}).tile && full->cell(1, {-32, 0}).tile == wide &&
                  pointers(*full).at({1, {1, 0}}) == pointers(*old_full).at({1, {1, 0}}),
              "Full fallback kept an absent chunk or copied an unchanged array");
        receiver.observe(source.capture(3, scene, {}));
        check(!receiver.pending() && receiver.retained_bytes() == 0 &&
                  !bound(receiver, scene, view).cells,
              "Removed actor retained copied ownership");
        // Stale begin is deferred until a fresh observation, not retried in a loop.
        receiver.observe(source.capture(3, scene, std::span(&view, 1)));
        request = receiver.next_request();
        source.capture(3, scene, {});
        receiver.receive(respond(source, *request));
        check(!receiver.next_request(), "Stale begin immediately retried old observations");
        receiver.clear();
        check(receiver.retained_bytes() == 0, "Stop retained copied values");
        // Repetition across two individually valid pages cannot change a frame.
        {
            TileMapPlaySource repeated_source;
            TileMapPlayReceiver repeated_receiver;
            auto value = view;
            value.cells = std::make_shared<const TileMapData>(original);
            repeated_receiver.observe(repeated_source.capture(1, scene, std::span(&value, 1)));
            step(repeated_source, repeated_receiver);
            auto first = repeated_receiver.next_request();
            const auto first_page = respond(repeated_source, *first);
            repeated_receiver.receive(first_page);
            auto second_request = repeated_receiver.next_request();
            auto second_page = respond(repeated_source, *second_request);
            second_page["data"][0] = first_page.at("data")[0];
            rejects([&] { repeated_receiver.receive(second_page); });
            check(!bound(repeated_receiver, scene, value).cells,
                  "Repeated chunk from another page published a partial frame");
            step(repeated_source, repeated_receiver); // Cancel the frozen source.
        }
        // Current copied values have a real aggregate budget, including private
        // instances whose cells no longer share their initial asset arrays.
        {
            TileMapPlaySource large_source;
            TileMapPlayReceiver large_receiver;
            std::vector<TileMapPlayView> large(4, view);
            unsigned private_flags = 0;
            for (auto& value : large) {
                value.entity = EntityId::generate();
                value.map_revision = "large-private-base";
                value.edited = true;
                value.cells = std::make_shared<const TileMapData>(
                    profile(id, set, 1024, 256, false, private_flags++));
            }
            large_receiver.observe(large_source.capture(99, scene, large));
            bool capped = false;
            for (unsigned operation = 0; operation < 2000 && !capped; ++operation) {
                try {
                    step(large_source, large_receiver);
                } catch (const std::runtime_error& error) {
                    capped =
                        std::string(error.what()).find("cache exceeds 64 MiB") != std::string::npos;
                    if (!capped)
                        throw;
                }
            }
            check(capped, "Four distinct maximum private values bypassed the copied cache budget");
            step(large_source,
                 large_receiver); // Cancel rejected candidate; retain admitted frames.
            check(large_receiver.retained_bytes() <= 64 * 1024 * 1024,
                  "Budget rejection retained the staged candidate");
            large_receiver.clear();
            check(large_receiver.retained_bytes() == 0, "Budgeted cache did not release on Stop");
        }
        // Shared maximum maps with small private changes must keep their shared
        // storage even when every update requires a full fallback on the wire.
        {
            TileMapPlaySource shared_source;
            TileMapPlayReceiver shared_receiver;
            const auto base = std::make_shared<const TileMapData>(profile(id, set, 1024, 256));
            std::vector<TileMapPlayView> values(4, view);
            for (unsigned index = 0; index < values.size(); ++index) {
                auto& value = values[index];
                value.entity = EntityId::generate();
                value.generation = index + 1;
                value.revision = 0;
                value.edited = false;
                value.map_revision = "shared-maximum-base";
                value.cells = base;
            }
            shared_receiver.observe(shared_source.capture(104, scene, values));
            drain(shared_source, shared_receiver);
            for (unsigned index = 0; index < values.size(); ++index) {
                const std::array<TileMapCellEdit, 1> edit{
                    {{1, {std::int32_t(index * 32), 0}, {wide, 7}}}};
                values[index].cells =
                    std::make_shared<const TileMapData>(base->with_cells(edit, defs));
                values[index].revision = 1;
                values[index].edited = true;
                shared_source.clear();
                shared_receiver.observe(shared_source.capture(104, scene, values));
                drain(shared_source, shared_receiver);
            }
            check(shared_receiver.retained_bytes() < 24 * 1024 * 1024,
                  "Full fallbacks expanded shared maximum maps into independent arrays");
            for (const auto& value : values)
                equal(*bound(shared_receiver, scene, value).cells, *value.cells);
        }
        std::cout << "Copied tile receiving roundtrip, atomic frames, frozen updates, sharing, "
                     "rejection/cancel/recovery and scope reset passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
