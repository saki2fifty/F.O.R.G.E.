#include "tile_map_play_receiver.hpp"
#include <algorithm>
#include <forge/render_scene.hpp>
#include <limits>
#include <set>
#include <stdexcept>

namespace forge {
namespace {
using Json = nlohmann::json;
constexpr std::size_t cache_limit = 64 * 1024 * 1024;
void require(bool ok, const char* text) {
    if (!ok)
        throw std::runtime_error(text);
}
std::uint64_t natural(const Json& value, std::uint64_t limit = UINT64_MAX) {
    require(value.is_number_unsigned() ||
                (value.is_number_integer() && value.get<std::int64_t>() >= 0),
            "Tile Play response requires an unsigned integer");
    const auto n = value.get<std::uint64_t>();
    require(n <= limit, "Tile Play response exceeds its integer profile");
    return n;
}
bool flag(const Json& row, const char* key) {
    require(row.at(key).is_boolean(), "Tile Play response requires a boolean");
    return row.at(key).get<bool>();
}
std::string bounded(const Json& row, const char* key, std::size_t maximum) {
    require(row.at(key).is_string(), "Tile Play response requires text");
    auto result = row.at(key).get<std::string>();
    require(result.size() <= maximum, "Tile Play response text exceeds its profile");
    return result;
}
std::int32_t coordinate(const Json& value) {
    require(value.is_number_integer(), "Tile Play chunk coordinate must be an integer");
    if (value.is_number_unsigned()) {
        require(value.get<std::uint64_t>() <= INT32_MAX,
                "Tile Play chunk coordinate exceeds int32");
        return std::int32_t(value.get<std::uint64_t>());
    }
    const auto result = value.get<std::int64_t>();
    require(result >= INT32_MIN && result <= INT32_MAX, "Tile Play chunk coordinate exceeds int32");
    return std::int32_t(result);
}
bool initial_match(const TileMapPlayView& frame, const TileMapPlayView& wanted, AssetId set) {
    return frame.cells && frame.definitions && !frame.edited && !frame.revision && !wanted.edited &&
           !wanted.revision && frame.cells->asset() == wanted.requested &&
           frame.definitions->asset() == set && frame.map_revision == wanted.map_revision &&
           frame.set_revision == wanted.set_revision;
}
} // namespace
void TileMapPlayReceiver::owner() const {
    require(owner_ == std::this_thread::get_id(),
            "Tile Play receiving state requires its PlaySession owner");
}
void TileMapPlayReceiver::clear() {
    owner();
    world_ = 0;
    scene_ = {};
    last_actor_ = {};
    wanted_.clear();
    frames_.clear();
    flight_.reset();
    transfer_.reset();
    cancel_ = 0;
    defer_ = false;
    diagnostic_.clear();
}
bool TileMapPlayReceiver::compatible(const TileMapPlayView& value) const {
    const auto at = wanted_.find(value.entity);
    return at != wanted_.end() && at->second.ready &&
           at->second.value.requested == value.requested &&
           at->second.value.generation == value.generation &&
           at->second.definitions == value.definitions->asset();
}
void TileMapPlayReceiver::observe(const Json& manifest) {
    owner();
    const auto world = natural(manifest.at("world"));
    const auto scene = manifest.at("scene").get<AssetId>();
    require(world && scene && manifest.at("maps").is_array() && manifest.at("maps").size() <= 256,
            "Invalid Tile Play manifest scope");
    std::map<EntityId, Wanted> candidate;
    for (const auto& row : manifest.at("maps")) {
        Wanted wanted;
        auto& value = wanted.value;
        value.entity = row.at("entity").get<EntityId>();
        value.requested = row.at("map").get<AssetId>();
        wanted.ready = flag(row, "ready");
        value.pending = flag(row, "pending");
        value.diagnostic = bounded(row, "diagnostic", 1024);
        require(value.entity && value.requested, "Tile Play manifest has an empty identity");
        if (wanted.ready) {
            value.generation = natural(row.at("generation"));
            value.revision = natural(row.at("revision"));
            wanted.definitions = row.at("tile_set").get<AssetId>();
            value.map_revision = bounded(row, "map_revision", 256);
            value.set_revision = bounded(row, "set_revision", 256);
            value.edited = flag(row, "edited");
            wanted.cells = natural(row.at("cells"), tile_map_cell_limit);
            wanted.chunks = natural(row.at("chunks"), tile_map_chunk_limit);
            require(value.generation && wanted.definitions && !value.map_revision.empty() &&
                        !value.set_revision.empty() && wanted.cells >= wanted.chunks &&
                        wanted.cells <= wanted.chunks * tile_chunk_cells,
                    "Tile Play manifest has incompatible counts or revisions");
        }
        require(candidate.emplace(value.entity, std::move(wanted)).second,
                "Tile Play manifest repeats an actor");
    }
    if (world != world_ || scene != scene_)
        clear();
    world_ = world;
    scene_ = scene;
    auto frames = frames_;
    std::erase_if(frames, [&](const auto& entry) {
        const auto at = candidate.find(entry.first);
        return at == candidate.end() || !at->second.ready ||
               at->second.value.requested != entry.second.requested ||
               at->second.value.generation != entry.second.generation;
    });
    // Reuse only a received, explicitly unedited value with exactly the same
    // adopted Map/Set revisions. This never loads an authored asset as fallback.
    for (const auto& [entity, wanted] : candidate) {
        const auto previous = frames_.find(entity);
        if (wanted.ready && previous != frames_.end() &&
            previous->second.generation == wanted.value.generation &&
            previous->second.requested == wanted.value.requested)
            require(previous->second.revision <= wanted.value.revision,
                    "Tile Play cell revision moved backwards within one instance");
        if (!wanted.ready || frames.contains(entity))
            continue;
        for (const auto& [other, known] : frames_) {
            (void)other;
            if (initial_match(known, wanted.value, wanted.definitions)) {
                auto value = wanted.value;
                value.cells = known.cells;
                value.definitions = known.definitions;
                frames.emplace(entity, std::move(value));
                break;
            }
        }
    }
    wanted_ = std::move(candidate);
    frames_ = std::move(frames);
    defer_ = false;
    if (transfer_ && !compatible(transfer_->value) && !flight_) {
        cancel_ = transfer_->token;
        transfer_.reset();
    }
}
std::optional<Json> TileMapPlayReceiver::next_request() {
    owner();
    if (flight_ || !world_)
        return {};
    Json request{{"world", world_}, {"scene", scene_}};
    if (cancel_) {
        flight_ = Flight{Operation::Cancel, {}, {}, cancel_};
        request["command"] = "tile_map_cancel";
        request["token"] = cancel_;
        return request;
    }
    if (transfer_) {
        Flight flight{transfer_->cursor < transfer_->total ? Operation::Page
                                                           : Operation::Acknowledge,
                      {},
                      {},
                      transfer_->token,
                      transfer_->cursor};
        request["command"] = flight.operation == Operation::Page ? "tile_map_page" : "tile_map_ack";
        request["token"] = flight.token;
        if (flight.operation == Operation::Page)
            request["cursor"] = flight.cursor;
        flight_ = std::move(flight);
        return request;
    }
    if (defer_ || wanted_.empty())
        return {};
    auto at = wanted_.upper_bound(last_actor_);
    for (std::size_t checked = 0; checked < wanted_.size(); ++checked) {
        if (at == wanted_.end())
            at = wanted_.begin();
        const auto& wanted = at->second;
        const auto frame = frames_.find(at->first);
        if (wanted.ready &&
            (frame == frames_.end() || frame->second.revision != wanted.value.revision)) {
            Flight flight{Operation::Begin, wanted};
            if (frame != frames_.end())
                flight.base = frame->second;
            request["command"] = "tile_map_begin";
            request["entity"] = at->first;
            request["generation"] = wanted.value.generation;
            request["revision"] = wanted.value.revision;
            request["base_generation"] = flight.base.cells ? flight.base.generation : 0;
            request["base_revision"] = flight.base.cells ? flight.base.revision : 0;
            last_actor_ = at->first;
            flight_ = std::move(flight);
            return request;
        }
        ++at;
    }
    return {};
}
std::size_t TileMapPlayReceiver::cache_bytes(const TileMapPlayView* replacement) const {
    std::vector<const TileMapData*> maps;
    std::set<const TileSetData*> sets;
    const auto add = [&](const TileMapPlayView& value) {
        maps.push_back(value.cells.get());
        sets.insert(value.definitions.get());
    };
    for (const auto& [entity, frame] : frames_)
        if (!replacement || entity != replacement->entity)
            add(frame);
    if (replacement)
        add(*replacement);
    auto bytes = TileMapData::resident_bytes(maps);
    for (const auto* set : sets)
        bytes += set->resident_bytes();
    return bytes;
}
std::size_t TileMapPlayReceiver::retained_bytes() const {
    owner();
    std::vector<const TileMapData*> maps;
    std::set<const TileSetData*> sets;
    for (const auto& [entity, frame] : frames_) {
        (void)entity;
        maps.push_back(frame.cells.get());
        sets.insert(frame.definitions.get());
    }
    if (transfer_) {
        maps.push_back(transfer_->value.cells.get());
        sets.insert(transfer_->value.definitions.get());
    }
    if (flight_ && flight_->base.cells) {
        maps.push_back(flight_->base.cells.get());
        sets.insert(flight_->base.definitions.get());
    }
    auto bytes = TileMapData::resident_bytes(maps);
    for (const auto* set : sets)
        bytes += set->resident_bytes();
    return bytes;
}
void TileMapPlayReceiver::complete() const {
    require(transfer_ && transfer_->cursor == transfer_->total &&
                transfer_->value.cells->occupied() == transfer_->cells &&
                transfer_->value.cells->chunk_count() == transfer_->chunks,
            "Tile Play completed transfer counts disagree");
    require(cache_bytes(&transfer_->value) <= cache_limit,
            "Tile Play copied view cache exceeds 64 MiB");
}
bool TileMapPlayReceiver::receive(const Json& payload) {
    owner();
    if (!flight_)
        return false;
    const auto flight = std::exchange(flight_, {});
    try {
        if (payload.contains("stale") && flag(payload, "stale")) {
            transfer_.reset();
            cancel_ = 0;
            defer_ = true;
            return true;
        }
        if (flight->operation == Operation::Cancel) {
            (void)flag(payload, "accepted");
            cancel_ = 0;
            return true;
        }
        if (flight->operation == Operation::Begin) {
            const auto token = natural(payload.at("token"));
            require(token, "Tile Play transfer has an empty token");
            cancel_ = token; // Retire a frozen sender even if header admission fails.
            const auto wanted = wanted_.find(flight->wanted.value.entity);
            if (wanted == wanted_.end() || !wanted->second.ready ||
                wanted->second.value.generation != flight->wanted.value.generation ||
                wanted->second.value.requested != flight->wanted.value.requested) {
                cancel_ = token;
                return true;
            }
            Transfer transfer;
            transfer.value = flight->wanted.value;
            transfer.token = token;
            transfer.total = natural(payload.at("count"), tile_map_chunk_limit * 2);
            transfer.cells = natural(payload.at("cells"), tile_map_cell_limit);
            transfer.chunks = natural(payload.at("chunks"), tile_map_chunk_limit);
            require(transfer.cells == flight->wanted.cells &&
                        transfer.chunks == flight->wanted.chunks,
                    "Tile Play begin disagrees with its observed revision");
            if (flag(payload, "full")) {
                transfer.full = true;
                const auto header = decode_tile_map_projection_header(
                    payload.at("header"), transfer.value.requested, flight->wanted.definitions);
                require(header.expected_cells == transfer.cells &&
                            header.expected_chunks == transfer.chunks,
                        "Tile Play header counts disagree");
                transfer.value.cells = std::make_shared<const TileMapData>(header.empty);
                const auto& keys = payload.at("keys");
                require(keys.is_array() && keys.size() == transfer.chunks &&
                            transfer.total == transfer.chunks,
                        "Full Tile Play chunk manifest disagrees with its counts");
                for (const auto& tuple : keys) {
                    require(tuple.is_array() && tuple.size() == 3,
                            "Full Tile Play chunk manifest has an invalid key");
                    const TileMapChunkKey key{natural(tuple[0]),
                                              {coordinate(tuple[1]), coordinate(tuple[2])}};
                    (void)header.empty.cell(key.layer, tile_cell_address({key.address, 0}));
                    (void)tile_cell_address({key.address, tile_chunk_cells - 1});
                    require(transfer.expected.insert(key).second,
                            "Full Tile Play chunk manifest repeats a key");
                }
                // Full pages overwrite every slot. Start from compatible received
                // storage and erase absent chunks first, preserving equal arrays
                // even when the sender has evicted its acknowledged baseline.
                const auto reusable = [&](const TileMapPlayView& known) {
                    return known.cells && known.cells->asset() == header.empty.asset() &&
                           known.cells->tile_set() == header.empty.tile_set() &&
                           known.cells->cell_size() == header.empty.cell_size() &&
                           known.cells->layers() == header.empty.layers();
                };
                std::shared_ptr<const TileMapData> storage;
                if (reusable(flight->base))
                    storage = flight->base.cells;
                else
                    for (const auto& [entity, known] : frames_) {
                        (void)entity;
                        if (reusable(known)) {
                            storage = known.cells;
                            break;
                        }
                    }
                if (storage) {
                    transfer.value.cells = storage;
                    std::vector<TileMapCellEdit> erase;
                    erase.reserve(tile_map_edit_limit);
                    const auto flush = [&] {
                        if (erase.empty())
                            return;
                        transfer.value.cells = std::make_shared<const TileMapData>(
                            transfer.value.cells->with_erased_cells(erase));
                        erase.clear();
                    };
                    storage->for_each_chunk([&](auto key, auto cells, auto) {
                        if (transfer.expected.contains(key))
                            return;
                        for (unsigned slot = 0; slot < tile_chunk_cells; ++slot)
                            if (cells[slot].tile) {
                                erase.push_back(
                                    {key.layer, tile_cell_address({key.address, slot}), {}});
                                if (erase.size() == tile_map_edit_limit)
                                    flush();
                            }
                    });
                    flush();
                }
                transfer.value.definitions =
                    std::make_shared<const TileSetData>(header.definitions);
                for (const auto& [entity, known] : frames_) {
                    (void)entity;
                    if (known.definitions->asset() == flight->wanted.definitions &&
                        known.set_revision == transfer.value.set_revision) {
                        require(tile_map_projection_header(header.empty, *known.definitions)
                                        .at("definitions") ==
                                    payload.at("header").at("definitions"),
                                "Tile Play reused Set revision has different definitions");
                        transfer.value.definitions = known.definitions;
                        break;
                    }
                }
            } else {
                require(flight->base.cells && flight->base.generation == transfer.value.generation,
                        "Tile Play delta has no matching received baseline");
                transfer.value.cells = flight->base.cells;
                transfer.value.definitions = flight->base.definitions;
            }
            transfer_ = std::move(transfer);
            cancel_ = 0;
            if (!transfer_->total)
                complete();
        } else {
            require(transfer_ && flight->token == transfer_->token,
                    "Tile Play reply has no frozen transfer");
            if (flight->operation == Operation::Page) {
                require(natural(payload.at("token")) == transfer_->token &&
                            natural(payload.at("cursor")) == transfer_->cursor,
                        "Tile Play page token/cursor disagrees");
                const auto next = natural(payload.at("next"), transfer_->total);
                const auto& data = payload.at("data");
                require(data.is_array() && !data.empty() &&
                            data.size() <= tile_projection_page_chunks &&
                            next == transfer_->cursor + data.size(),
                        "Tile Play page progress disagrees");
                if (!compatible(transfer_->value)) {
                    cancel_ = transfer_->token;
                    transfer_.reset();
                    return true;
                }
                auto cells = apply_tile_map_projection_page(*transfer_->value.cells,
                                                            *transfer_->value.definitions, data);
                for (const auto& row : data) {
                    const TileMapChunkKey key{
                        row.at("layer").get<std::uint64_t>(),
                        {row.at("x").get<std::int32_t>(), row.at("y").get<std::int32_t>()}};
                    require(!transfer_->full || transfer_->expected.contains(key),
                            "Full Tile Play page has an undeclared chunk");
                    require(transfer_->seen.insert(key).second,
                            "Tile Play transfer repeats a chunk across pages");
                }
                transfer_->value.cells = std::make_shared<const TileMapData>(std::move(cells));
                transfer_->cursor = next;
                if (next == transfer_->total)
                    complete();
            } else {
                require(flag(payload, "accepted"), "Runtime refused Tile Play acknowledgement");
                if (!compatible(transfer_->value)) {
                    transfer_.reset();
                    return true;
                }
                complete();
                if (compatible(transfer_->value))
                    frames_.insert_or_assign(transfer_->value.entity, transfer_->value);
                transfer_.reset();
                // Reuse only actually received initial values with matching
                // adopted revisions; no authored resource loading occurs here.
                for (const auto& [entity, wanted] : wanted_) {
                    if (!wanted.ready || frames_.contains(entity))
                        continue;
                    for (const auto& [other, known] : frames_) {
                        (void)other;
                        if (initial_match(known, wanted.value, wanted.definitions)) {
                            auto value = wanted.value;
                            value.cells = known.cells;
                            value.definitions = known.definitions;
                            frames_.emplace(entity, std::move(value));
                            break;
                        }
                    }
                }
                diagnostic_.clear();
            }
        }
        return true;
    } catch (const std::exception& error) {
        diagnostic_ = error.what();
        if (transfer_)
            cancel_ = transfer_->token;
        transfer_.reset();
        defer_ = true;
        throw;
    }
}
bool TileMapPlayReceiver::pending() const {
    owner();
    if (transfer_ || flight_ || cancel_)
        return true;
    for (const auto& [entity, wanted] : wanted_) {
        if (wanted.value.pending)
            return true;
        const auto frame = frames_.find(entity);
        if (wanted.ready &&
            (frame == frames_.end() || frame->second.revision != wanted.value.revision))
            return true;
    }
    return false;
}
void TileMapPlayReceiver::bind(RenderScene& scene) const {
    owner();
    for (auto& map : scene.tile_maps) {
        map.runtime_instance = true;
        map.cells.reset();
        map.definitions.reset();
        map.generation = map.revision = 0;
        map.pending = true;
        map.diagnostic.clear();
        if (scene.scene != scene_)
            continue;
        const auto wanted = wanted_.find(map.entity);
        if (wanted == wanted_.end() || wanted->second.value.requested != map.renderer.map.id)
            continue;
        map.pending = wanted->second.ready || wanted->second.value.pending;
        map.diagnostic = wanted->second.value.diagnostic;
        const auto frame = frames_.find(map.entity);
        if (frame == frames_.end())
            continue;
        map.cells = frame->second.cells;
        map.definitions = frame->second.definitions;
        map.generation = frame->second.generation;
        map.revision = frame->second.revision;
        map.pending = false;
    }
}
} // namespace forge
