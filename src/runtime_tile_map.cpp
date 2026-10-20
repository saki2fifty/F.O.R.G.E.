#include "runtime_tile_map.hpp"
#include <algorithm>
#include <atomic>
#include <forge/render_components.hpp>
#include <limits>

namespace forge::detail {
namespace {
constexpr std::size_t pending_byte_limit = 4 * 1024 * 1024;
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
    bool current = false;
    void clear() noexcept {
        snapshot.reset();
        map.clear();
        definitions.clear();
        requested = {};
        requested_revision.clear();
        diagnostic.clear();
        current = false;
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
auto instance(WorldContext& context, EntityRef target) {
    (void)queue(context);
    const auto resolved = context.resolve(target);
    require(resolved.state == WorldContext::ResolveState::Available,
            "Tile Map target is missing, unresolved or ambiguous");
    auto entity = context.world().entity(resolved.entity);
    require(entity.has<TileMapRenderer>() && entity.has<RuntimeTileMapState>(),
            "Target has no realized runtime Tile Map");
    return std::pair{entity, entity.get<RuntimeTileMapState>().value};
}
bool ready(const RuntimeResourceStatus& status) {
    return status.state == "ready" && !status.requested_revision.empty() &&
           status.requested_revision == status.retained_revision;
}
void adopt(WorldContext& context, Instance& state, bool reset) {
    require(bool(state.requested), "Tile Map has no assigned asset");
    require(context.services().available(Capability::Resources),
            "Runtime Tile Map resource provider is unavailable");
    auto resources = context.services().resources();
    state.map.select(resources, RuntimeResourceKind::TileMap, state.requested);
    const auto map_status = resources->inspect(state.map.token);
    state.requested_revision = map_status.requested_revision;
    require(ready(map_status), "Tile Map asset is not current and ready");
    const auto map = resources->tile_map(state.map.token);
    require(bool(map), "Tile Map has no admitted CPU lease");
    state.definitions.select(resources, RuntimeResourceKind::TileSet, map->tile_set());
    const auto set_status = resources->inspect(state.definitions.token);
    require(ready(set_status), "Tile Map definitions are not current and ready");
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
        state.requested_revision.clear();
        if (!requested) {
            state.clear();
            continue;
        }
        state.requested = requested;
        try {
            adopt(context, state, false);
        } catch (const std::exception& error) {
            state.diagnostic = std::string(error.what()).substr(0, 1024);
        }
    }
}
RuntimeTileMapStatus inspect_runtime_tile_map(WorldContext& context, EntityRef target) {
    const auto [entity, state] = instance(context, target);
    RuntimeTileMapStatus status;
    status.requested = entity.get<TileMapRenderer>().map.id;
    status.requested_revision = state->requested_revision;
    status.diagnostic = state->diagnostic;
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
                adopt(context, *state, true);
            else {
                require(snapshot->revision != std::numeric_limits<std::uint64_t>::max(),
                        "Runtime Tile Map cell revision exhausted");
                auto candidate = std::make_shared<RuntimeTileMapSnapshot>(*snapshot);
                candidate->cells =
                    snapshot->cells.with_cells(command.writes, snapshot->definitions.get());
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
} // namespace forge::detail
