#include "runtime_tile_map.hpp"
#include "tile_map_projection.hpp"
#include <algorithm>
#include <atomic>
#include <forge/physics_components.hpp>
#include <forge/render_components.hpp>
#include <forge/render_scene.hpp>
#include <limits>
#include <set>

namespace forge::detail {
namespace {
constexpr std::size_t pending_byte_limit = 4 * 1024 * 1024;
constexpr std::size_t checkpoint_byte_limit = 8 * 1024 * 1024;
// Same copied-cell profile as Play projection; compact sparse JSON must not
// expand into unbounded chunk arrays during private reconstruction.
constexpr std::size_t checkpoint_cell_limit = 64 * 1024 * 1024;
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
struct Subscription {
    std::weak_ptr<RuntimeResourceService> owner;
    std::uint64_t token = 0;
    AssetId asset;
    void clear() noexcept {
        if (token)
            if (auto service = owner.lock())
                try {
                    service->release(token);
                } catch (...) { /* Provider already closed. */
                }
        token = 0;
        asset = {};
        owner.reset();
    }
    ~Subscription() { clear(); }
    void select(const std::shared_ptr<RuntimeResourceService>& service, RuntimeResourceKind kind,
                AssetId id) {
        if (token && asset == id && owner.lock() == service)
            return;
        clear();
        token = service->request(kind, id);
        owner = service;
        asset = id;
    }
};
struct Instance {
    AssetId requested;
    Subscription map, definitions;
    std::shared_ptr<const RuntimeTileMapSnapshot> snapshot;
    std::string requested_revision, diagnostic;
    bool current = false, pending = false;
    void clear() noexcept {
        snapshot.reset();
        map.clear();
        definitions.clear();
        requested = {};
        requested_revision.clear();
        diagnostic.clear();
        current = false;
        pending = false;
    }
};
// This private component owns mutable instance state. It is never inherited,
// reflected, serialized into Scene, or mirrored in a resource-manager database.
struct RuntimeTileMapState {
    std::shared_ptr<Instance> value;
};
struct Request {
    std::string module;
    EntityRef target;
    flecs::entity_t native = 0;
    std::uint64_t generation = 0;
    bool reset = false;
    std::vector<TileMapCellEdit> writes;
    RuntimeTileEditResult result;
};
struct Queue {
    std::map<std::uint64_t, Request> requests;
    std::size_t pending_bytes = 0;
    std::uint64_t next_generation = 1;
    bool observing = false;
    TileMapPhysicsAdmission physics_admission;
};
struct RuntimeTileMapHost {
    std::shared_ptr<Queue> queue;
};
Queue& queue(WorldContext& context) {
    require(context.on_owner_thread() && context.role() == WorldRole::Runtime,
            "Runtime Tile Maps require their runtime world owner thread");
    return *context.world().get<RuntimeTileMapHost>().queue;
}
void boundary(WorldContext& context) {
    (void)queue(context);
    require(!ecs_is_deferred(context.world()) && !ecs_stage_is_readonly(context.world()),
            "Runtime Tile Maps publish only outside Flecs progress/defer");
}
std::uint64_t generation(Queue& values) {
    require(values.next_generation != std::numeric_limits<std::uint64_t>::max(),
            "Runtime Tile Map generation space exhausted");
    return values.next_generation++;
}
auto target_entity(WorldContext& context, EntityRef target) {
    (void)queue(context);
    const auto resolved = context.resolve(target);
    require(resolved.state == WorldContext::ResolveState::Available,
            "Tile Map target is missing, unresolved or ambiguous");
    auto entity = context.world().entity(resolved.entity);
    require(entity.has<TileMapRenderer>(), "Target has no Tile Map Renderer");
    return entity;
}
auto instance(WorldContext& context, EntityRef target) {
    auto entity = target_entity(context, target);
    require(entity.has<RuntimeTileMapState>(), "Target has no realized runtime Tile Map");
    return std::pair{entity, entity.get<RuntimeTileMapState>().value};
}
bool ready(const RuntimeResourceStatus& status) {
    return status.state == "ready" && !status.requested_revision.empty() &&
           status.requested_revision == status.retained_revision;
}
void require_ready(Instance& state, const RuntimeResourceStatus& status, const char* label) {
    if (ready(status))
        return;
    state.pending = status.state == "queued" || status.state == "dependency pending" ||
                    status.state == "loading" || status.state == "replacing";
    throw std::runtime_error(std::string(label) + ": " +
                             (status.diagnostic.empty() ? status.state : status.diagnostic));
}
void adopt(WorldContext& context, Instance& state, bool reset, EntityRef target) {
    state.pending = false;
    require(bool(state.requested), "Tile Map has no assigned asset");
    require(context.services().available(Capability::Resources),
            "Runtime Tile Map resource provider is unavailable");
    auto resources = context.services().resources();
    state.map.select(resources, RuntimeResourceKind::TileMap, state.requested);
    const auto map_status = resources->inspect(state.map.token);
    state.requested_revision = map_status.requested_revision;
    require_ready(state, map_status, "Tile Map asset is not current and ready");
    const auto map = resources->tile_map(state.map.token);
    require(bool(map), "Tile Map has no admitted CPU lease");
    state.definitions.select(resources, RuntimeResourceKind::TileSet, map->tile_set());
    const auto set_status = resources->inspect(state.definitions.token);
    require_ready(state, set_status, "Tile Map definitions are not current and ready");
    const auto definitions = resources->tile_set(state.definitions.token);
    require(bool(definitions) && definitions->asset() == map->tile_set(),
            "Tile Map definition binding differs");
    const auto previous = state.snapshot;
    const bool same =
        previous && previous->asset && previous->definitions &&
        previous->asset.identity().revision == map.identity().revision &&
        previous->definitions.identity().revision == definitions.identity().revision &&
        previous->cells.asset() == map->asset();
    if (same && !reset) {
        state.current = true;
        state.diagnostic.clear();
        return;
    }
    // Explicit reset or assignment of a different root can discard runtime
    // changes. A new revision of the same edited asset cannot silently do so.
    require(reset || !previous || !previous->edited || previous->cells.asset() != map->asset(),
            "Edited Tile Map retains its adopted revision; reset the instance to adopt new assets");
    map->for_each_chunk([&](auto, auto cells, auto) {
        for (const auto& cell : cells)
            if (cell.tile)
                (void)definitions->tile(cell.tile);
    });
    auto candidate = std::make_shared<RuntimeTileMapSnapshot>();
    candidate->cells = map.get();
    candidate->asset = map;
    candidate->definitions = definitions;
    candidate->definition_values = definitions.get();
    if (const auto& admission = queue(context).physics_admission)
        admission(target, candidate->cells, reset);
    candidate->generation = generation(queue(context));
    state.snapshot = std::move(candidate);
    state.current = true;
    state.diagnostic.clear();
}
Request& owned(Queue& values, const std::string& module, std::uint64_t token) {
    const auto found = values.requests.find(token);
    require(found != values.requests.end() && found->second.module == module,
            "Unknown, released or foreign runtime Tile Map request");
    return found->second;
}
std::vector<EntityRef> assigned_maps(WorldContext& context) {
    std::vector<EntityRef> result;
    context.world().each([&](flecs::entity entity, const TileMapRenderer& renderer) {
        if (renderer.map.id)
            if (const auto ref = context.reference(entity.id()))
                result.push_back(*ref);
    });
    std::sort(result.begin(), result.end());
    return result;
}
bool active_map(flecs::entity entity) {
    return entity.get<TileMapRenderer>().enabled ||
           (entity.has<TileMapCollider2D>() && entity.has<PhysicsBody2D>() &&
            entity.get<PhysicsBody2D>().enabled);
}
std::uint64_t checkpoint_integer(const Json& value) {
    require(value.is_number_unsigned() ||
                (value.is_number_integer() && value.get<std::int64_t>() >= 0),
            "Tile Map checkpoint requires an exact nonnegative integer");
    return value.get<std::uint64_t>();
}
} // namespace
void register_runtime_tile_maps(WorldContext& context) {
    auto& world = context.world();
    world.component<RuntimeTileMapState>("forge.runtime.TileMapState")
        .add(flecs::OnInstantiate, flecs::DontInherit);
    world.component<RuntimeTileMapHost>("forge.runtime.TileMapRequests")
        .add(flecs::OnInstantiate, flecs::DontInherit);
    if (context.role() == WorldRole::Runtime)
        world.set<RuntimeTileMapHost>({std::make_shared<Queue>()});
}
void synchronize_runtime_tile_maps(WorldContext& context) {
    if (context.role() != WorldRole::Runtime)
        return;
    boundary(context);
    auto& host = queue(context);
    if (!host.observing) {
        // Core has registered the public component's native Meta by this point.
        // An early typed observer would register it under a different C++ name.
        context.world()
            .observer<TileMapRenderer>()
            .event(flecs::OnRemove)
            .each([](flecs::entity entity, const TileMapRenderer&) {
                if (entity.has<RuntimeTileMapState>())
                    entity.get<RuntimeTileMapState>().value->clear();
            });
        host.observing = true;
    }
    std::vector<flecs::entity_t> maps, removed;
    context.world().each([&](flecs::entity entity, const TileMapRenderer&) {
        if (context.reference(entity.id()))
            maps.push_back(entity.id());
    });
    context.world().each([&](flecs::entity entity, const RuntimeTileMapState&) {
        if (!entity.has<TileMapRenderer>())
            removed.push_back(entity.id());
    });
    for (auto id : removed)
        context.world().entity(id).remove<RuntimeTileMapState>();
    for (auto id : maps) {
        auto entity = context.world().entity(id);
        if (!entity.has<RuntimeTileMapState>())
            entity.set<RuntimeTileMapState>({std::make_shared<Instance>()});
        auto& state = *entity.get<RuntimeTileMapState>().value;
        const auto requested = entity.get<TileMapRenderer>().map.id;
        state.current = false;
        state.pending = false;
        state.requested_revision.clear();
        if (!requested) {
            state.clear();
            continue;
        }
        state.requested = requested;
        try {
            adopt(context, state, false, context.reference(id).value());
        } catch (const std::exception& error) {
            state.diagnostic = std::string(error.what()).substr(0, 1024);
        }
    }
}
bool prepare_runtime_tile_maps(WorldContext& context) {
    if (context.role() != WorldRole::Runtime)
        return true;
    boundary(context);
    bool pending = false;
    std::string failure;
    context.world().each([&](flecs::entity entity, const TileMapRenderer& renderer) {
        if (!active_map(entity) || !renderer.map.id || !failure.empty())
            return;
        const auto reference = context.reference(entity.id());
        if (!reference)
            return;
        try {
            const auto status = inspect_runtime_tile_map(context, *reference);
            if (!status.ready || status.adopted != status.requested) {
                if (status.pending)
                    pending = true;
                else
                    failure = status.diagnostic.empty() ? "Tile Map has no ready runtime snapshot"
                                                        : status.diagnostic;
            }
        } catch (const std::exception& e) {
            failure = e.what();
        }
    });
    // Throw after iteration releases Flecs' table lock.
    if (!failure.empty())
        throw std::runtime_error("Scene preparation: " + failure);
    return !pending;
}
RuntimeTileMapStatus inspect_runtime_tile_map(WorldContext& context, EntityRef target) {
    const auto entity = target_entity(context, target);
    RuntimeTileMapStatus status;
    status.requested = entity.get<TileMapRenderer>().map.id;
    if (!entity.has<RuntimeTileMapState>()) {
        status.diagnostic = "Tile Map is waiting for runtime synchronization";
        status.pending = bool(status.requested);
        return status;
    }
    const auto state = entity.get<RuntimeTileMapState>().value;
    status.requested_revision = state->requested_revision;
    status.diagnostic = state->diagnostic;
    status.pending = state->pending;
    if (const auto& snapshot = state->snapshot;
        snapshot && snapshot->asset && snapshot->definitions) {
        status.ready = true;
        status.current = state->current && status.requested == state->requested;
        status.adopted = snapshot->cells.asset();
        status.tile_set = snapshot->cells.tile_set();
        status.adopted_revision = snapshot->asset.identity().revision;
        status.generation = snapshot->generation;
        status.revision = snapshot->revision;
        status.edited = snapshot->edited;
    }
    return status;
}
std::shared_ptr<const RuntimeTileMapSnapshot> runtime_tile_map_snapshot(WorldContext& context,
                                                                        EntityRef target) {
    const auto state = instance(context, target).second;
    require(state->snapshot && state->snapshot->asset && state->snapshot->definitions,
            "Runtime Tile Map has no ready retained snapshot");
    return state->snapshot;
}
void bind_runtime_tile_maps(WorldContext& context, RenderScene& scene) {
    (void)queue(context);
    for (auto& map : scene.tile_maps) {
        map.runtime_instance = true;
        map.pending = false;
        map.generation = map.revision = 0;
        map.cells.reset();
        map.definitions.reset();
        map.diagnostic.clear();
        try {
            const EntityRef target{scene.scene, map.entity};
            const auto status = inspect_runtime_tile_map(context, target);
            map.diagnostic = status.diagnostic;
            map.pending = status.pending;
            if (!status.ready)
                continue;
            const auto snapshot = runtime_tile_map_snapshot(context, target);
            map.cells = {snapshot, &snapshot->cells};
            map.definitions = {snapshot, &snapshot->definition_values};
            map.generation = snapshot->generation;
            map.revision = snapshot->revision;
        } catch (const std::exception& error) {
            map.diagnostic = std::string(error.what()).substr(0, 1024);
        }
    }
}
static std::uint64_t request(WorldContext& context, const std::string& module, EntityRef target,
                             std::uint64_t expected, std::span<const TileMapCellEdit> writes,
                             bool reset) {
    auto& values = queue(context);
    require(valid_module_id(module) && expected,
            "Tile Map request needs module and adopted generation");
    require((reset && writes.empty()) ||
                (!reset && !writes.empty() && writes.size() <= tile_map_edit_limit),
            "Tile Map request has an invalid cell count");
    require(values.requests.size() < 256 &&
                std::count_if(values.requests.begin(), values.requests.end(),
                              [&](const auto& row) { return row.second.module == module; }) < 64,
            "Tile Map request limit reached (64/module,256/world)");
    const auto bytes = writes.size() * sizeof(TileMapCellEdit);
    require(bytes <= pending_byte_limit - values.pending_bytes,
            "Pending Tile Map cell requests exceed4MiB");
    const auto [entity, state] = instance(context, target);
    const auto snapshot = runtime_tile_map_snapshot(context, target);
    require(snapshot->generation == expected &&
                entity.get<TileMapRenderer>().map.id == snapshot->cells.asset(),
            "Runtime Tile Map request targets a stale binding");
    if (!reset)
        snapshot->cells.validate_cells(writes, snapshot->definitions.get());
    static std::atomic<std::uint64_t> next{1};
    auto token = next.load();
    do {
        require(token != std::numeric_limits<std::uint64_t>::max(),
                "Tile Map request token space exhausted");
    } while (!next.compare_exchange_weak(token, token + 1));
    Request value{module, target, entity.id(), expected, reset, {writes.begin(), writes.end()}, {}};
    values.requests.emplace(token, std::move(value));
    values.pending_bytes += bytes;
    return token;
}
std::uint64_t request_runtime_tile_cells(WorldContext& context, const std::string& module,
                                         EntityRef target, std::uint64_t expected,
                                         std::span<const TileMapCellEdit> writes) {
    return request(context, module, target, expected, writes, false);
}
std::uint64_t request_runtime_tile_reset(WorldContext& context, const std::string& module,
                                         EntityRef target, std::uint64_t expected) {
    return request(context, module, target, expected, {}, true);
}
RuntimeTileEditResult inspect_runtime_tile_edit(WorldContext& context, const std::string& module,
                                                std::uint64_t token) {
    return owned(queue(context), module, token).result;
}
bool release_runtime_tile_edit(WorldContext& context, const std::string& module,
                               std::uint64_t token) {
    auto& values = queue(context);
    const auto found = values.requests.find(token);
    if (found == values.requests.end() || found->second.module != module)
        return false;
    values.pending_bytes -= found->second.writes.size() * sizeof(TileMapCellEdit);
    values.requests.erase(found);
    return true;
}
void publish_runtime_tile_edits(WorldContext& context) {
    if (context.role() != WorldRole::Runtime)
        return;
    boundary(context);
    auto& values = queue(context);
    for (auto& [token, command] : values.requests) {
        (void)token;
        if (command.result.state != RuntimeTileEditState::Pending)
            continue;
        try {
            const auto [entity, state] = instance(context, command.target);
            const auto snapshot = runtime_tile_map_snapshot(context, command.target);
            require(entity.id() == command.native && snapshot->generation == command.generation &&
                        entity.get<TileMapRenderer>().map.id == snapshot->cells.asset(),
                    "Runtime Tile Map target was removed, reassigned or replaced");
            if (command.reset)
                adopt(context, *state, true, command.target);
            else {
                require(snapshot->revision != std::numeric_limits<std::uint64_t>::max(),
                        "Runtime Tile Map cell revision exhausted");
                auto candidate = std::make_shared<RuntimeTileMapSnapshot>(*snapshot);
                candidate->cells =
                    snapshot->cells.with_cells(command.writes, snapshot->definitions.get());
                if (const auto& admission = values.physics_admission)
                    admission(command.target, candidate->cells, true);
                ++candidate->revision;
                candidate->edited = true;
                state->snapshot = std::move(candidate);
            }
            command.result = {RuntimeTileEditState::Applied,
                              state->snapshot->generation,
                              state->snapshot->revision,
                              {}};
        } catch (const std::exception& error) {
            command.result.state = RuntimeTileEditState::Failed;
            command.result.diagnostic = std::string(error.what()).substr(0, 1024);
        }
        values.pending_bytes -= command.writes.size() * sizeof(TileMapCellEdit);
        std::vector<TileMapCellEdit>().swap(command.writes);
    }
}
void set_tile_map_physics_admission(WorldContext& context, TileMapPhysicsAdmission admission) {
    boundary(context);
    auto& slot = queue(context).physics_admission;
    require(!admission || !slot, "Runtime Tile Map physics admission already has an owner");
    slot = std::move(admission);
}
Json checkpoint_runtime_tile_maps(WorldContext& context) {
    boundary(context);
    const auto& values = queue(context);
    for (const auto& [token, request] : values.requests)
        if (request.result.state == RuntimeTileEditState::Pending)
            return {};
    Json entries = Json::array();
    std::size_t bytes = 128; // Root fields, integer frontier and separators.
    const auto charge = [&](std::size_t count) {
        require(count <= checkpoint_byte_limit - bytes,
                "Tile Map checkpoint exceeds whole recovery byte budget");
        bytes += count;
    };
    std::vector<const TileMapData*> borrowed;
    std::size_t reconstructed = 0;
    for (const auto ref : assigned_maps(context)) {
        const auto entity = target_entity(context, ref);
        const bool edited = entity.has<RuntimeTileMapState>() &&
                            entity.get<RuntimeTileMapState>().value->snapshot &&
                            entity.get<RuntimeTileMapState>().value->snapshot->edited;
        // An unused disabled renderer may intentionally reference an unavailable
        // asset. It must not prevent unrelated active state from being recovered.
        // Retain edited instances even when their renderer is disabled.
        if (!active_map(entity) && !edited)
            continue;
        const auto status = inspect_runtime_tile_map(context, ref);
        if (!status.ready || !status.current || status.requested != status.adopted)
            return {};
        const auto snapshot = runtime_tile_map_snapshot(context, ref);
        // Full edited projections reconstruct independent arrays. Live COW
        // sharing must not make an unrestorable checkpoint appear to fit.
        if (snapshot->edited) {
            const auto count = snapshot->cells.resident_bytes();
            require(count <= checkpoint_cell_limit - reconstructed,
                    "Tile Map checkpoint exceeds copied cell memory reconstruction profile");
            reconstructed += count;
        } else
            borrowed.push_back(&snapshot->cells);
        require(TileMapData::resident_bytes(borrowed) <= checkpoint_cell_limit - reconstructed,
                "Tile Map checkpoint exceeds copied cell memory reconstruction profile");
        Json cells;
        if (snapshot->edited) {
            cells = {{"header",
                      tile_map_projection_header(snapshot->cells, snapshot->definition_values)},
                     {"pages", Json::array()}};
            charge(cells.dump().size());
            const auto chunks = tile_map_projection_changes(snapshot->cells);
            for (std::size_t at = 0; at < chunks.size(); at += tile_projection_page_chunks) {
                auto page = tile_map_projection_page(
                    snapshot->cells,
                    std::span(chunks).subspan(
                        at, std::min(tile_projection_page_chunks, chunks.size() - at)));
                charge(page.dump().size() + 1);
                cells["pages"].push_back(std::move(page));
            }
        }
        Json entry{{"entity", ref},
                   {"map", snapshot->cells.asset()},
                   {"definitions", snapshot->cells.tile_set()},
                   {"map_revision", snapshot->asset.identity().revision},
                   {"set_revision", snapshot->definitions.identity().revision},
                   {"generation", snapshot->generation},
                   {"revision", snapshot->revision},
                   {"edited", snapshot->edited},
                   {"cells", nullptr}};
        charge(entry.dump().size() + 1);
        entry["cells"] = std::move(cells);
        entries.push_back(std::move(entry));
    }
    Json result{{"version", 1},
                {"next_generation", values.next_generation},
                {"entries", std::move(entries)}};
    require(result.dump().size() <= checkpoint_byte_limit,
            "Tile Map checkpoint exceeds whole recovery byte budget");
    return result;
}
void restore_runtime_tile_maps(WorldContext& context, const Json& input) {
    boundary(context);
    auto& values = queue(context);
    require(values.requests.empty(), "Tile Map recovery requires a fresh request queue");
    require(input.is_object() && input.size() == 3 && input.at("version").is_number_integer() &&
                input.at("version") == 1 && input.dump().size() <= checkpoint_byte_limit,
            "Invalid Tile Map checkpoint version/size/fields");
    const auto next_generation = checkpoint_integer(input.at("next_generation"));
    require(next_generation != 0, "Invalid Tile Map checkpoint generation frontier");
    const auto& entries = input.at("entries");
    const auto targets = assigned_maps(context);
    require(entries.is_array() && entries.size() <= targets.size(),
            "Tile Map checkpoint exceeds the assigned instances");
    std::set<EntityRef> seen;
    std::set<std::uint64_t> generations;
    std::vector<std::pair<std::shared_ptr<Instance>, std::shared_ptr<const RuntimeTileMapSnapshot>>>
        candidates;
    std::vector<const TileMapData*> retained;
    for (const auto& row : entries) {
        require(row.is_object() && row.size() == 9, "Invalid Tile Map checkpoint instance fields");
        const auto ref = row.at("entity").get<EntityRef>();
        require(std::binary_search(targets.begin(), targets.end(), ref) && seen.insert(ref).second,
                "Unknown or duplicate Tile Map checkpoint target");
        const auto [entity, state] = instance(context, ref);
        const auto selected = runtime_tile_map_snapshot(context, ref);
        require(!selected->edited && selected->revision == 0 && state->current &&
                    entity.get<TileMapRenderer>().map.id == selected->cells.asset() &&
                    row.at("map").get<AssetId>() == selected->cells.asset() &&
                    row.at("definitions").get<AssetId>() == selected->cells.tile_set() &&
                    row.at("map_revision") == selected->asset.identity().revision &&
                    row.at("set_revision") == selected->definitions.identity().revision,
                "Tile Map recovery source binding differs or candidate is not fresh");
        auto candidate = std::make_shared<RuntimeTileMapSnapshot>(*selected);
        candidate->generation = checkpoint_integer(row.at("generation"));
        candidate->revision = checkpoint_integer(row.at("revision"));
        require(candidate->generation && candidate->generation < next_generation &&
                    generations.insert(candidate->generation).second &&
                    row.at("edited").is_boolean(),
                "Invalid or duplicate Tile Map checkpoint generation");
        candidate->edited = row.at("edited").get<bool>();
        const auto& cells = row.at("cells");
        require(candidate->edited ? candidate->revision != 0
                                  : candidate->revision == 0 && cells.is_null(),
                "Tile Map checkpoint edit revision/payload mismatch");
        if (candidate->edited) {
            require(cells.is_object() && cells.size() == 2 && cells.at("pages").is_array() &&
                        cells.at("pages").size() <=
                            (tile_map_chunk_limit + tile_projection_page_chunks - 1) /
                                tile_projection_page_chunks,
                    "Invalid Tile Map checkpoint cell pages");
            const auto header = decode_tile_map_projection_header(
                cells.at("header"), selected->cells.asset(), selected->cells.tile_set());
            require(cells.at("header").at("definitions") ==
                        tile_map_projection_header(selected->cells, selected->definition_values)
                            .at("definitions"),
                    "Tile Map checkpoint definitions differ from selected assets");
            auto restored = header.empty;
            std::set<TileMapChunkKey> chunks;
            for (const auto& page : cells.at("pages")) {
                const auto next =
                    apply_tile_map_projection_page(restored, header.definitions, page);
                retained.push_back(&next);
                require(TileMapData::resident_bytes(retained) <= checkpoint_cell_limit,
                        "Tile Map recovery exceeds copied cell memory profile");
                retained.pop_back();
                // Reuse the admitted codec to validate numeric key fields before
                // inspecting them here. Full captures never repeat or erase chunks.
                for (const auto& chunk : page) {
                    const TileMapChunkKey key{
                        chunk.at("layer").get<std::uint64_t>(),
                        {chunk.at("x").get<std::int32_t>(), chunk.at("y").get<std::int32_t>()}};
                    require(!chunk.at("cells").empty() && chunks.insert(key).second,
                            "Tile Map checkpoint repeats or erases a full-capture chunk");
                }
                restored = next;
            }
            require(restored.occupied() == header.expected_cells &&
                        restored.chunk_count() == header.expected_chunks,
                    "Tile Map checkpoint cell/chunk totals differ");
            // This existing delta-admission boundary checks immutable identity,
            // layer definitions and cell dimensions against the selected asset.
            (void)tile_map_projection_changes(restored, &selected->cells);
            candidate->cells = std::move(restored);
        }
        retained.push_back(&candidate->cells);
        require(TileMapData::resident_bytes(retained) <= checkpoint_cell_limit,
                "Tile Map recovery exceeds copied cell memory profile");
        candidates.emplace_back(state, std::move(candidate));
    }
    for (const auto ref : targets)
        require(!active_map(target_entity(context, ref)) || seen.contains(ref),
                "Tile Map checkpoint omits an active instance");
    for (auto& [state, snapshot] : candidates)
        state->snapshot = std::move(snapshot);
    // Nonrestored disabled instances can have prepared newer generations in the
    // fresh world. Never reuse those values for a later reset or replacement.
    values.next_generation = std::max(next_generation, values.next_generation);
}
} // namespace forge::detail
