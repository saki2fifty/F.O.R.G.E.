#include "authored_schema.hpp"
#include "bounded_json.hpp"
#include "runtime_entity_creation.hpp"
#include <cstring>
#include <forge/native_sdk.h>
#include <forge/native_sdk.hpp>
#include <forge/native_sdk_identity.h>
#include <forge/project_paths.hpp>
#include <set>
#include <thread>
#ifdef FORGE_ENABLE_NATIVE_SDK
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#endif
namespace forge {
namespace {
using Json = nlohmann::json;
static_assert(unsigned(NavStatus::Success) == FORGE_SDK_NAV_SUCCESS &&
              unsigned(NavStatus::Partial) == FORGE_SDK_NAV_PARTIAL &&
              unsigned(NavStatus::Missing) == FORGE_SDK_NAV_MISSING &&
              unsigned(NavStatus::Stale) == FORGE_SDK_NAV_STALE &&
              unsigned(NavStatus::StartOutside) == FORGE_SDK_NAV_START_OUTSIDE &&
              unsigned(NavStatus::EndOutside) == FORGE_SDK_NAV_END_OUTSIDE &&
              unsigned(NavStatus::NoPath) == FORGE_SDK_NAV_NO_PATH &&
              unsigned(NavStatus::Limit) == FORGE_SDK_NAV_LIMIT &&
              unsigned(NavStatus::Invalid) == FORGE_SDK_NAV_INVALID &&
              unsigned(NavStatus::Unavailable) == FORGE_SDK_NAV_UNAVAILABLE);
const std::set<std::string> builtins{"forge.core",      "forge.transforms", "forge.prefabs",
                                     "forge.input",     "forge.physics",    "forge.audio",
                                     "forge.animation", "forge.navigation", "forge.ui",
                                     "forge.resources", "forge.game"};
#ifdef FORGE_ENABLE_NATIVE_SDK
struct Library {
    void* handle{};
    ~Library() {
#ifdef _WIN32
        if (handle)
            FreeLibrary(static_cast<HMODULE>(handle));
#else
        if (handle)
            dlclose(handle);
#endif
    }
};
std::string bounded(const char* p, std::size_t maximum) {
    if (!p)
        throw std::runtime_error("Null native SDK metadata");
    std::size_t n = 0;
    while (n <= maximum && p[n])
        ++n;
    if (!n || n > maximum)
        throw std::runtime_error("Invalid native SDK metadata length");
    return {p, n};
}
struct Bridge {
    ModuleContext& context;
    ForgeSdkWorldV1 host{};
    std::thread::id owner = std::this_thread::get_id();
    enum class Stage {
        Schema,
        Registered,
        Starting,
        Preparing,
        Running,
        Stopped
    } stage = Stage::Schema;
    static Bridge& get(void* p) {
        if (!p)
            throw std::runtime_error("Null SDK context");
        auto& bridge = *static_cast<Bridge*>(p);
        if (bridge.owner != std::this_thread::get_id())
            throw std::runtime_error("SDK requires its owner thread");
        return bridge;
    }
    std::set<std::uint64_t> resources;
    std::set<std::uint64_t> entity_requests;
    std::set<std::uint64_t> game_requests;
    std::map<std::string, UiAction> ui_events;
    bool game_callable() const {
        return context.role == WorldRole::Runtime && runtime_active() &&
               context.services.available(Capability::Game);
    }
    static Json game_json(const char* input) {
        const auto text = bounded(input, 1024 * 1024);
        return asset_detail::parse_bounded_json(std::as_bytes(std::span(text.data(), text.size())),
                                                1024 * 1024);
    }
    static uint32_t copy_json(const Json& value, char* output, uint32_t capacity) {
        const auto text = value.dump();
        if (text.size() >= 1024 * 1024)
            throw std::runtime_error("Game service response exceeds limit");
        if (output && capacity > text.size())
            std::memcpy(output, text.c_str(), text.size() + 1);
        return static_cast<uint32_t>(text.size() + 1);
    }
    bool entities_callable() const {
        return context.owner && context.role == WorldRole::Runtime && runtime_active();
    }
    void release_entities() {
        if (context.owner)
            for (auto token : entity_requests)
                detail::release_runtime_entity(*context.owner, context.id, token);
        entity_requests.clear();
    }
    void release_resources() {
        if (context.services.available(Capability::Resources))
            for (auto token : resources)
                context.services.resources()->release(token);
        resources.clear();
    }
    bool resources_callable() const {
        return context.role == WorldRole::Runtime && runtime_active() &&
               context.services.available(Capability::Resources);
    }
    bool runtime_active() const {
        return stage == Stage::Starting || stage == Stage::Preparing || stage == Stage::Running;
    }

    explicit Bridge(ModuleContext& c) : context(c) {
        host.size = sizeof(host);
        host.version = FORGE_NATIVE_SDK_ABI;
        host.world = c.world.c_ptr();
        host.context = this;
        host.role = role_mask(c.role);
        for (auto cap : {Capability::Diagnostics, Capability::Profiling})
            if (c.services.available(cap))
                host.capabilities |= capability(cap);
        host.fixed_phase = c.world.entity("forge.runtime.Gameplay").add(flecs::Phase).id();
        host.fixed_tag = c.world.id<FixedSimulation>();
        host.post_physics_phase =
            c.world.entity("forge.runtime.PostPhysics").add(flecs::Phase).id();
        host.game_query = [](void* p, char* output, uint32_t capacity) -> uint32_t {
            try {
                auto& bridge = Bridge::get(p);
                if (!bridge.game_callable())
                    return 0;
                return copy_json(bridge.context.services.game()->query(), output, capacity);
            } catch (...) {
                return 0;
            }
        };
        host.ui_allow_value_action = [](void* p, const char* command) -> int32_t {
            try {
                auto& bridge = Bridge::get(p);
                if (bridge.stage != Stage::Starting)
                    return 0;
                bridge.context.services.ui()->allow_value_action(bounded(command, 64));
                return 1;
            } catch (...) {
                return 0;
            }
        };
        host.ui_publish_json = [](void* p, const char* entity, const char* name,
                                  const char* value) -> int32_t {
            try {
                auto& bridge = Bridge::get(p);
                auto& c = bridge.context;
                if (!bridge.runtime_active() ||
                    (!c.input && !c.controls && bridge.stage != Stage::Preparing))
                    return 0;
                c.services.ui()->publish(EntityId::parse(bounded(entity, 36)), bounded(name, 64),
                                         game_json(value));
                return 1;
            } catch (...) {
                return 0;
            }
        };
        host.ui_poll_event = [](void* p, const char* command, char* output,
                                uint32_t capacity) -> uint32_t {
            try {
                auto& bridge = Bridge::get(p);
                if (!bridge.runtime_active() || (!bridge.context.input && !bridge.context.controls))
                    return 0;
                const auto name = bounded(command, 64);
                if (!bridge.ui_events.contains(name)) {
                    const auto event = bridge.context.services.ui()->poll_action(name);
                    if (!event)
                        return 0;
                    bridge.ui_events.emplace(name, *event);
                }
                const auto& event = bridge.ui_events.at(name);
                const auto needed = copy_json(
                    {{"entity", event.entity}, {"command", event.command}, {"value", event.value}},
                    output, capacity);
                if (output && capacity >= needed)
                    bridge.ui_events.erase(name);
                return needed;
            } catch (...) {
                return 0;
            }
        };
        host.game_request = [](void* p, const char* command) -> uint64_t {
            try {
                auto& bridge = Bridge::get(p);
                if (!bridge.game_callable() || bridge.game_requests.size() >= 128 ||
                    (!bridge.context.input && !bridge.context.controls))
                    return 0;
                auto service = bridge.context.services.game();
                const auto token = service->request(bridge.context.id, game_json(command));
                try {
                    bridge.game_requests.insert(token);
                } catch (...) {
                    service->release(bridge.context.id, token);
                    throw;
                }
                return token;
            } catch (...) {
                return 0;
            }
        };
        host.game_inspect = [](void* p, uint64_t token, char* output,
                               uint32_t capacity) -> uint32_t {
            try {
                auto& bridge = Bridge::get(p);
                if (!bridge.game_callable() || !bridge.game_requests.contains(token))
                    return 0;
                const auto status =
                    bridge.context.services.game()->inspect(bridge.context.id, token);
                return copy_json(
                    {{"state", status.state}, {"value", status.value}, {"error", status.error}},
                    output, capacity);
            } catch (...) {
                return 0;
            }
        };
        host.game_release = [](void* p, uint64_t token) -> int32_t {
            try {
                auto& bridge = Bridge::get(p);
                if (!bridge.game_callable() || !bridge.game_requests.contains(token))
                    return 0;
                bridge.context.services.game()->release(bridge.context.id, token);
                bridge.game_requests.erase(token);
                return 1;
            } catch (...) {
                return 0;
            }
        };
        host.game_save_schema = [](void* p, const ForgeSdkSaveSchemaV1* registration) -> int32_t {
            try {
                auto& bridge = Bridge::get(p);
                if (!bridge.game_callable() || bridge.stage != Stage::Starting || !registration ||
                    registration->size != sizeof(*registration) || !registration->validate ||
                    registration->migration_count > 128 ||
                    (registration->migration_count &&
                     (!registration->migrate || !registration->migration_versions)))
                    return 0;
                const auto native = *registration;
                GameSaveSchema schema;
                schema.version = native.version;
                schema.validate = [validate = native.validate,
                                   userdata = native.userdata](const GameSave& save) {
                    const auto input = Json{{"scene", save.scene}, {"data", save.data}}.dump();
                    char error[1024]{};
                    if (!validate(userdata, input.c_str(), error, sizeof(error))) {
                        error[1023] = 0;
                        throw std::runtime_error(std::string("Game save validation failed: ") +
                                                 error);
                    }
                };
                for (uint32_t i = 0; i < native.migration_count; ++i) {
                    const auto version = native.migration_versions[i];
                    if (!schema.migrations
                             .emplace(
                                 version,
                                 [migrate = native.migrate, userdata = native.userdata,
                                  version](GameSave save) {
                                     const auto input =
                                         Json{{"scene", save.scene}, {"data", save.data}}.dump();
                                     std::vector<char> output(1024 * 1024);
                                     if (!migrate(userdata, version, input.c_str(), output.data(),
                                                  static_cast<uint32_t>(output.size())) ||
                                         output.back() != 0)
                                         throw std::runtime_error("Game save migration failed");
                                     const auto result = game_json(output.data());
                                     return GameSave{result.at("scene").get<AssetId>(),
                                                     result.at("data")};
                                 })
                             .second)
                        throw std::runtime_error("Duplicate save migration version");
                }
                bridge.context.services.game()->save_schema(bridge.context.id, std::move(schema),
                                                            bridge.context.code);
                return 1;
            } catch (...) {
                return 0;
            }
        };
        host.entity_request = [](void* p, const char* scene, const char* name) -> uint64_t {
            try {
                auto& b = Bridge::get(p);
                if (!b.entities_callable() || b.entity_requests.size() >= 64)
                    return 0;
                const auto scene_id =
                    scene && *scene ? AssetId::parse(bounded(scene, 36)) : AssetId{};
                const auto token = detail::request_runtime_entity(*b.context.owner, b.context.id,
                                                                  scene_id, bounded(name, 255));
                try {
                    b.entity_requests.insert(token);
                } catch (...) {
                    detail::release_runtime_entity(*b.context.owner, b.context.id, token);
                    throw;
                }
                return token;
            } catch (...) {
                return 0;
            }
        };
        host.entity_inspect = [](void* p, uint64_t token, ForgeSdkEntityV1* out) -> int32_t {
            if (!out || out->size != sizeof(*out))
                return 0;
            *out = {};
            out->size = sizeof(*out);
            try {
                auto& b = Bridge::get(p);
                if (!b.entities_callable() || !b.entity_requests.contains(token))
                    return 0;
                const auto value =
                    detail::inspect_runtime_entity(*b.context.owner, b.context.id, token);
                out->state = static_cast<uint32_t>(value.state);
                out->native_entity = value.native_entity;
                auto copy = [](auto& dest, const std::string& source) {
                    const auto n = std::min(source.size(), sizeof(dest) - 1);
                    std::memcpy(dest, source.data(), n);
                    dest[n] = 0;
                };
                if (value.reference.scene)
                    copy(out->scene_uuid, value.reference.scene.str());
                if (value.reference.entity)
                    copy(out->entity_uuid, value.reference.entity.str());
                copy(out->diagnostic, value.diagnostic);
                return 1;
            } catch (...) {
                return 0;
            }
        };
        host.entity_release = [](void* p, uint64_t token) -> int32_t {
            try {
                auto& b = Bridge::get(p);
                if (!b.entities_callable() || !b.entity_requests.contains(token))
                    return 0;
                const bool released =
                    detail::release_runtime_entity(*b.context.owner, b.context.id, token);
                b.entity_requests.erase(token);
                return released ? 1 : 0;
            } catch (...) {
                return 0;
            }
        };
        host.resource_request = [](void* p, uint32_t kind, const char* id,
                                   uint32_t texture_variant) -> uint64_t {
            try {
                auto& b = Bridge::get(p);
                if (!b.resources_callable() || b.resources.size() >= 64)
                    return 0;
                auto service = b.context.services.resources();
                auto token = service->request(static_cast<RuntimeResourceKind>(kind),
                                              AssetId::parse(bounded(id, 36)),
                                              static_cast<RuntimeTextureVariant>(texture_variant));
                try {
                    b.resources.insert(token);
                } catch (...) {
                    service->release(token);
                    throw;
                }
                return token;
            } catch (...) {
                return 0;
            }
        };
        host.resource_inspect = [](void* p, uint64_t token, ForgeSdkResourceV1* out) -> int32_t {
            if (!out || out->size != sizeof(*out))
                return 0;
            *out = {};
            out->size = sizeof(*out);
            try {
                auto& b = Bridge::get(p);
                if (!b.resources_callable() || !b.resources.contains(token))
                    return 0;
                const auto value = b.context.services.resources()->inspect(token);
                auto copy = [](auto& dest, const std::string& source) {
                    const auto n = std::min(source.size(), sizeof(dest) - 1);
                    std::memcpy(dest, source.data(), n);
                    dest[n] = 0;
                };
                copy(out->state, value.state);
                copy(out->requested_revision, value.requested_revision);
                copy(out->retained_revision, value.retained_revision);
                copy(out->diagnostic, value.diagnostic);
                out->source_generation = value.source_generation;
                return 1;
            } catch (...) {
                return 0;
            }
        };
        host.resource_release = [](void* p, uint64_t token) -> int32_t {
            try {
                auto& b = Bridge::get(p);
                if (!b.resources_callable() || !b.resources.contains(token))
                    return 0;
                const bool released = b.context.services.resources()->release(token);
                b.resources.erase(token);
                return released ? 1 : 0;
            } catch (...) {
                return 0;
            }
        };
        host.authoring_type = [](void* p, uint64_t type, const char* key, uint32_t version,
                                 const char* defaults, const char* category, char* error,
                                 uint32_t capacity) -> int32_t {
            if (!error || !capacity || capacity > 8192)
                return 0;
            error[0] = 0;
            try {
                auto& b = Bridge::get(p);
                if (b.stage != Stage::Schema)
                    throw std::runtime_error("Authoring opt-in is schema-registration-only");
                const auto bytes = bounded(defaults, 65536);
                auto value = asset_detail::parse_bounded_json(std::as_bytes(std::span(bytes)),
                                                              65536, 16384, 8);
                detail::opt_in_authoring(b.context.world, type, bounded(key, 128), b.context.id,
                                         version, std::move(value), bounded(category, 255));
                return 1;
            } catch (const std::exception& e) {
                const auto n = std::min(std::strlen(e.what()), std::size_t(capacity - 1));
                std::memcpy(error, e.what(), n);
                error[n] = 0;
                return 0;
            } catch (...) {
                error[0] = 0;
                return 0;
            }
        };
        host.query_capability = [](void* p, uint32_t cap, uint32_t version,
                                   ForgeSdkCapabilityV1* out) -> int32_t {
            if (!out || out->size != sizeof(*out))
                return 0;
            *out = {sizeof(*out), 0, 0, 0, 0};
            try {
                auto& b = Bridge::get(p);
                if (!cap || (cap & (cap - 1)) ||
                    (cap & ~(known_capabilities | FORGE_SDK_INPUT | FORGE_SDK_RUNTIME_ENTITIES |
                             FORGE_SDK_CONTROL_INPUT)))
                    return 0;
                out->version = FORGE_SDK_CAPABILITY_VERSION;
                out->flags = FORGE_SDK_OWNER_THREAD;
                bool fixed = cap == FORGE_SDK_INPUT || cap == FORGE_SDK_PHYSICS ||
                             cap == FORGE_SDK_AUDIO || cap == FORGE_SDK_NAVIGATION;
                if (fixed)
                    out->flags |= FORGE_SDK_FIXED_ONLY;
                if (cap == FORGE_SDK_CONTROL_INPUT)
                    out->flags |= FORGE_SDK_CONTROL_ONLY;
                if (version != out->version)
                    return 1;
                bool available = (cap == FORGE_SDK_INPUT || cap == FORGE_SDK_CONTROL_INPUT)
                                     ? b.context.role == WorldRole::Runtime && b.runtime_active()
                                     : b.context.services.available(static_cast<Capability>(cap));
                if (cap == FORGE_SDK_RESOURCES)
                    available = b.resources_callable();
                if (cap == FORGE_SDK_RUNTIME_ENTITIES)
                    available = b.entities_callable();
                if (cap == FORGE_SDK_GAME)
                    available = b.game_callable();
                if (fixed && !b.runtime_active())
                    available = false;
                out->available = available;
                out->callable = available && (!fixed || b.context.input != nullptr);
                if (cap == FORGE_SDK_CONTROL_INPUT)
                    out->callable = available && b.context.controls != nullptr;
                if (cap == FORGE_SDK_UI)
                    out->callable =
                        available && b.runtime_active() &&
                        (b.context.input || b.context.controls || b.stage == Stage::Preparing);
                return 1;
            } catch (...) {
                return 0;
            }
        };
        host.profile_sample = [](void* p, const char* name, double seconds) -> int32_t {
            try {
                auto& c = Bridge::get(p).context;
                c.services.record_profile(c.id, bounded(name, 128), seconds,
                                          c.input ? c.input->tick : 0);
                return 1;
            } catch (...) {
                return 0;
            }
        };
        host.ui_allow_action = [](void* p, const char* name) -> int32_t {
            try {
                if (!p)
                    return 0;
                auto& bridge = Bridge::get(p);
                if (bridge.stage != Stage::Starting)
                    return 0;
                bridge.context.services.ui()->allow_action(bounded(name, 64));
                return 1;
            } catch (...) {
                return 0;
            }
        };
        host.ui_publish_number = [](void* p, const char* entity, const char* name,
                                    double value) -> int32_t {
            try {
                if (!p)
                    return 0;
                auto& b = Bridge::get(p);
                auto& c = b.context;
                if (!c.input && !c.controls && b.stage != Stage::Preparing)
                    return 0;
                c.services.ui()->publish(EntityId::parse(bounded(entity, 36)), bounded(name, 64),
                                         value);
                return 1;
            } catch (...) {
                return 0;
            }
        };
        host.ui_poll_action = [](void* p, const char* name, char* entity,
                                 uint32_t capacity) -> int32_t {
            try {
                if (!p || !entity || capacity < 37)
                    return -1;
                entity[0] = 0;
                auto& c = Bridge::get(p).context;
                if ((!c.input && !c.controls) || !c.services.available(Capability::Ui))
                    return 0;
                auto action = c.services.ui()->poll_action(bounded(name, 64));
                if (!action)
                    return 0;
                const auto id = action->entity.str();
                std::memcpy(entity, id.c_str(), 37);
                return 1;
            } catch (...) {
                return -1;
            }
        };
        host.navigation_query = [](void* p, const char* asset, uint32_t operation,
                                   const double* start, const double* end,
                                   ForgeSdkNavResultV1* out) -> int32_t {
            if (!p || !start || !out || out->size != sizeof(*out) || operation > 1 ||
                (operation == 1 && !end) || !out->xyz || out->capacity == 0 || out->capacity > 64)
                return 0;
            out->count = 0;
            out->status = uint32_t(NavStatus::Unavailable);
            try {
                auto& c = Bridge::get(p).context;
                if (!c.input)
                    return 0;
                if (!c.services.available(Capability::Navigation))
                    return 1;
                AssetRef<NavMeshAsset> ref{AssetId::parse(bounded(asset, 36))};
                auto service = c.services.navigation();
                auto value = operation == 0
                                 ? service->project_point(ref, {start[0], start[1], start[2]})
                                 : service->find_path(ref, {start[0], start[1], start[2]},
                                                      {end[0], end[1], end[2]});
                if (value.points.size() > out->capacity) {
                    out->status = uint32_t(NavStatus::Limit);
                    return 1;
                }
                out->status = uint32_t(value.status);
                out->count = uint32_t(value.points.size());
                for (unsigned i = 0; i < out->count; ++i)
                    for (unsigned j = 0; j < 3; ++j)
                        out->xyz[i * 3 + j] = value.points[i][j];
                return 1;
            } catch (...) {
                out->status = uint32_t(NavStatus::Invalid);
                return 1;
            }
        };
        host.audio_source = [](void* p, const char* scene, const char* entity,
                               uint32_t play) -> int32_t {
            try {
                auto& c = Bridge::get(p).context;
                if (!c.input || play > 1)
                    return 0;
                const EntityRef ref{AssetId::parse(bounded(scene, 36)),
                                    EntityId::parse(bounded(entity, 36))};
                auto audio = c.services.audio();
                if (play)
                    audio->play(ref);
                else
                    audio->stop_source(ref);
                return 1;
            } catch (...) {
                return 0;
            }
        };
        host.raycast = [](void* p, const double* origin, const double* displacement,
                          ForgeSdkPhysicsHitV1* out) -> int32_t {
            try {
                auto& c = Bridge::get(p).context;
                if (!origin || !displacement || !out || out->size != sizeof(*out) || !c.input)
                    return -1;
                *out = {};
                out->size = sizeof(*out);
                auto hit = c.services.physics()->raycast(
                    {origin[0], origin[1], origin[2]},
                    {displacement[0], displacement[1], displacement[2]});
                if (!hit)
                    return 0;
                auto scene = hit->entity.scene.str(), entity = hit->entity.entity.str();
                std::memcpy(out->scene, scene.c_str(), 37);
                std::memcpy(out->entity, entity.c_str(), 37);
                for (unsigned i = 0; i < 3; ++i) {
                    out->position[i] = hit->position[i];
                    out->normal[i] = hit->normal[i];
                }
                out->fraction = hit->fraction;
                return 1;
            } catch (...) {
                return -1;
            }
        };
        host.character_state = [](void* p, const char* scene, const char* entity,
                                  ForgeSdkCharacterV1* out) -> int32_t {
            if (!out || out->size != sizeof(*out))
                return 0;
            *out = {};
            out->size = sizeof(*out);
            try {
                auto& c = Bridge::get(p).context;
                if (!c.input)
                    return 0;
                const EntityRef ref{AssetId::parse(bounded(scene, 36)),
                                    EntityId::parse(bounded(entity, 36))};
                const auto value = c.services.physics()->character(ref);
                out->ground = static_cast<uint32_t>(value.ground);
                out->crouched = value.crouched;
                out->shape_change_blocked = value.shape_change_blocked;
                out->jump_accepted = value.jump_accepted;
                const Double3 position{value.position.x, value.position.y, value.position.z};
                for (unsigned i = 0; i < 3; ++i) {
                    out->position[i] = position[i];
                    out->velocity[i] = value.velocity[i];
                    out->ground_position[i] = value.ground_position[i];
                    out->ground_normal[i] = value.ground_normal[i];
                    out->ground_velocity[i] = value.ground_velocity[i];
                }
                const float q[]{value.rotation.x, value.rotation.y, value.rotation.z,
                                value.rotation.w};
                std::copy(q, q + 4, out->rotation_xyzw);
                if (value.supporting_entity) {
                    std::memcpy(out->support_scene, value.supporting_entity->scene.str().c_str(),
                                37);
                    std::memcpy(out->support_entity, value.supporting_entity->entity.str().c_str(),
                                37);
                }
                return 1;
            } catch (...) {
                return 0;
            }
        };
        host.character_command = [](void* p, const char* scene, const char* entity, uint32_t op,
                                    const double* value, const float* q, float speed,
                                    uint32_t flag) -> int32_t {
            try {
                auto& c = Bridge::get(p).context;
                if (!c.input || op > 3 || flag > 1 || ((op == 0 || op == 3) && !value) ||
                    (op == 3 && !q))
                    return 0;
                const EntityRef ref{AssetId::parse(bounded(scene, 36)),
                                    EntityId::parse(bounded(entity, 36))};
                auto service = c.services.physics();
                if (op == 0)
                    service->move_character(ref, {value[0], value[1], value[2]});
                else if (op == 1)
                    service->jump_character(ref, speed);
                else if (op == 2)
                    service->crouch_character(ref, flag != 0);
                else
                    service->place_character(ref, {value[0], value[1], value[2]},
                                             {q[0], q[1], q[2], q[3]}, flag != 0);
                return 1;
            } catch (...) {
                return 0;
            }
        };
        host.raycast_filtered = [](void* p, const double* origin, const double* displacement,
                                   uint32_t mask, uint32_t sensors,
                                   ForgeSdkPhysicsHitV1* out) -> int32_t {
            if (!out || out->size != sizeof(*out))
                return -1;
            *out = {};
            out->size = sizeof(*out);
            try {
                auto& c = Bridge::get(p).context;
                if (!c.input || !origin || !displacement || sensors > 1)
                    return -1;
                const auto hit = c.services.physics()->raycast_filtered(
                    {origin[0], origin[1], origin[2]},
                    {displacement[0], displacement[1], displacement[2]}, {mask, sensors != 0});
                if (!hit)
                    return 0;
                std::memcpy(out->scene, hit->entity.scene.str().c_str(), 37);
                std::memcpy(out->entity, hit->entity.entity.str().c_str(), 37);
                for (unsigned i = 0; i < 3; ++i) {
                    out->position[i] = hit->position[i];
                    out->normal[i] = hit->normal[i];
                }
                out->fraction = hit->fraction;
                return 1;
            } catch (...) {
                return -1;
            }
        };
        host.shape_cast = [](void* p, const ForgeSdkSweepV1* request,
                             ForgeSdkPhysicsHitV1* out) -> int32_t {
            if (!out || out->size != sizeof(*out))
                return -1;
            *out = {};
            out->size = sizeof(*out);
            try {
                auto& c = Bridge::get(p).context;
                if (!c.input || !request || request->size != sizeof(*request) ||
                    request->include_sensors > 1)
                    return -1;
                PhysicsSweep sweep;
                sweep.shape = PhysicsSweep::Shape(request->shape);
                sweep.origin = {request->origin[0], request->origin[1], request->origin[2]};
                sweep.rotation = {request->rotation[0], request->rotation[1], request->rotation[2],
                                  request->rotation[3]};
                for (unsigned i = 0; i < 3; ++i) {
                    sweep.dimensions[i] = request->dimensions[i];
                    sweep.displacement[i] = request->displacement[i];
                }
                const auto hit = c.services.physics()->shape_cast(
                    sweep, {request->layer_mask, request->include_sensors != 0});
                if (!hit)
                    return 0;
                std::memcpy(out->scene, hit->entity.scene.str().c_str(), 37);
                std::memcpy(out->entity, hit->entity.entity.str().c_str(), 37);
                for (unsigned i = 0; i < 3; ++i) {
                    out->position[i] = hit->position[i];
                    out->normal[i] = hit->normal[i];
                }
                out->fraction = hit->fraction;
                return 1;
            } catch (...) {
                return -1;
            }
        };
        host.physics_move = [](void* p, const char* scene, const char* entity,
                               const double* position, const float* rotation, uint32_t motion,
                               uint32_t clear) -> int32_t {
            try {
                auto& c = Bridge::get(p).context;
                if (!position || !rotation || motion > 1 || clear > 1 || !c.input)
                    return 0;
                EntityRef ref{AssetId::parse(bounded(scene, 36)),
                              EntityId::parse(bounded(entity, 36))};
                LocalTranslation target{position[0], position[1], position[2]};
                LocalRotation q{rotation[0], rotation[1], rotation[2], rotation[3]};
                auto service = c.services.physics();
                if (motion)
                    service->move_kinematic(ref, target, q);
                else
                    service->teleport(ref, target, q, clear != 0);
                return 1;
            } catch (...) {
                return 0;
            }
        };
        host.read_action = [](void* p, const char* id, ForgeSdkActionV1* out) -> int32_t {
            try {
                auto& c = Bridge::get(p).context;
                if (!out || out->size != sizeof(*out) || !c.input)
                    return 0;
                *out = {};
                out->size = sizeof(*out);
                auto action = ActionId::parse(bounded(id, 36));
                const auto it = c.input->actions.find(action);
                if (it == c.input->actions.end())
                    return 0;
                const auto& a = it->second;
                *out = {
                    sizeof(*out), uint32_t(a.held), uint32_t(a.pressed), uint32_t(a.released), a.x,
                    a.y,          c.input->tick};
                return 1;
            } catch (...) {
                return 0;
            }
        };
        host.read_control = [](void* p, const char* id, ForgeSdkActionV1* out) -> int32_t {
            if (!out || out->size != sizeof(*out))
                return 0;
            *out = {};
            out->size = sizeof(*out);
            try {
                auto& b = Bridge::get(p);
                if (!b.runtime_active() || !b.context.controls)
                    return 0;
                const auto& snapshot = *b.context.controls;
                const auto it = snapshot.actions.find(ActionId::parse(bounded(id, 36)));
                if (it == snapshot.actions.end())
                    return 0;
                const auto& a = it->second;
                *out = {
                    sizeof(*out), uint32_t(a.held), uint32_t(a.pressed), uint32_t(a.released), a.x,
                    a.y,          snapshot.tick};
                return 1;
            } catch (...) {
                return 0;
            }
        };
        host.diagnostic = [](void* p, uint32_t severity, const char* text) -> int32_t {
            try {
                auto& c = Bridge::get(p).context;
                if (severity > 4 || !c.services.available(Capability::Diagnostics))
                    return 0;
                Diagnostic d{static_cast<Severity>(severity), "module", bounded(text, 8192), {}};
                d.context.module = c.id;
                d.context.world_role = world_role_name(c.role);
                if (c.input)
                    d.context.tick = c.input->tick;
                c.services.emit(std::move(d));
                return 1;
            } catch (...) {
                return 0;
            }
        };
    }
};
#endif
} // namespace
void validate_project_modules(const nlohmann::json& project) {
    if (!project.contains("modules"))
        return;
    const auto& modules = project.at("modules");
    if (!modules.is_array() || modules.size() > 64)
        throw std::runtime_error("Project modules require an array of at most 64 declarations");
    std::set<std::string> seen;
    std::vector<ModuleRequirement> graph;
    for (const auto& id : builtins)
        graph.push_back({id, {}, 0});
    for (const auto& m : modules) {
        const auto id = canonical_module_id(m.is_string() ? m.get<std::string>()
                                                          : m.at("id").get<std::string>());
        if (!valid_module_id(id) || !seen.insert(id).second)
            throw std::runtime_error("Invalid/duplicate project module: " + id);
        if (m.is_string()) {
            if (!builtins.contains(id))
                throw std::runtime_error("Unknown built-in module: " + id);
            continue;
        }
        if (builtins.contains(id) || m.at("sdk") != "experimental-1" ||
            m.at("implementation").get<std::string>().empty())
            throw std::runtime_error("Invalid native project module declaration: " + id);
        const auto fingerprint = m.at("fingerprint").get<std::string>();
        if (fingerprint.size() != 64 ||
            fingerprint.find_first_not_of("0123456789abcdef") != std::string::npos)
            throw std::runtime_error("Invalid SDK fingerprint: " + id);
        (void)ProjectPaths::normalize(std::filesystem::u8path(m.at("library").get<std::string>()));
        auto deps = m.at("dependencies").get<std::vector<std::string>>();
        if (deps.size() > 64 ||
            std::set<std::string>(deps.begin(), deps.end()).size() != deps.size())
            throw std::runtime_error("Invalid project module dependencies");
        graph.push_back({id, std::move(deps), 0});
    }
    (void)module_order(graph, 0);
}
EngineModule load_native_sdk(const std::filesystem::path& path, const std::string& id,
                             const std::string& implementation, ServiceAccess services) {
    try {
#ifndef FORGE_ENABLE_NATIVE_SDK
        (void)path;
        (void)implementation;
        throw std::runtime_error(
            "Direct Flecs modules require the shared native-sdk build profile");
#else
        if (!path.is_absolute() || !valid_module_id(id))
            throw std::runtime_error(
                "SDK library requires an absolute path and namespaced module ID");
        auto code = std::make_shared<Library>();
#ifdef _WIN32
        code->handle =
            LoadLibraryExW(path.c_str(), nullptr,
                           LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        auto entry = code->handle ? reinterpret_cast<ForgeNativeSdkEntryV1>(GetProcAddress(
                                        static_cast<HMODULE>(code->handle), "forge_native_sdk_v1"))
                                  : nullptr;
#else
        code->handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        auto entry = code->handle ? reinterpret_cast<ForgeNativeSdkEntryV1>(
                                        dlsym(code->handle, "forge_native_sdk_v1"))
                                  : nullptr;
#endif
        if (!entry)
            throw std::runtime_error("Cannot load native SDK library/entry");
        const auto* api = entry();
        if (!api || api->size != sizeof(*api) || api->version != FORGE_NATIVE_SDK_ABI ||
            !api->register_schema)
            throw std::runtime_error("Incompatible native SDK descriptor ABI");
        if (bounded(api->fingerprint, 64) != FORGE_NATIVE_SDK_FINGERPRINT)
            throw std::runtime_error("Native SDK fingerprint mismatch");
        if (api->flecs_init_identity != &ecs_init || api->flecs_os_identity != &ecs_os_api)
            throw std::runtime_error("Native module does not share the host Flecs implementation");
        if (bounded(api->module_id, 128) != id ||
            bounded(api->implementation_version, 128) != implementation)
            throw std::runtime_error("Native module identity/implementation mismatch");
        if (api->dependency_count > 64 || (api->dependency_count && !api->dependencies))
            throw std::runtime_error("Invalid native module dependencies");
        EngineModule result;
        result.id = id;
        result.implementation = implementation;
        result.code = code;
        for (uint32_t i = 0; i < api->dependency_count; ++i)
            result.dependencies.push_back(bounded(api->dependencies[i], 128));
        if (std::set<std::string>(result.dependencies.begin(), result.dependencies.end()).size() !=
            result.dependencies.size())
            throw std::runtime_error("Duplicate native module dependency");
        result.schema_roles = api->schema_roles;
        result.runtime_roles = api->runtime_roles;
        result.required_services = api->required_capabilities;
        result.allowed_services = api->allowed_capabilities;
        result.schemas = [api](ModuleContext& c) {
            auto bridge = std::make_shared<Bridge>(c);
            c.state = bridge;
            char error[1024]{};
            if (!api->register_schema(&bridge->host, error, sizeof(error))) {
                error[1023] = 0;
                throw std::runtime_error(std::string("Native schema registration failed: ") +
                                         error);
            }
            bridge->stage = Bridge::Stage::Registered;
        };
        result.start = [api](ModuleContext& c) {
            auto& bridge = *static_cast<Bridge*>(c.state.get());
            bridge.stage = Bridge::Stage::Starting;
            for (auto cap : {Capability::Physics, Capability::Audio, Capability::Navigation,
                             Capability::Ui, Capability::Resources, Capability::Game})
                if (c.services.available(cap))
                    static_cast<Bridge*>(c.state.get())->host.capabilities |= capability(cap);
            char error[1024]{};
            if (api->start &&
                !api->start(&static_cast<Bridge*>(c.state.get())->host, error, sizeof(error))) {
                error[1023] = 0;
                bridge.stage = Bridge::Stage::Stopped;
                throw std::runtime_error(std::string("Native runtime registration failed: ") +
                                         error);
            }
            bridge.stage = Bridge::Stage::Running;
        };
        result.controls = [api](ModuleContext& c) {
            auto& bridge = *static_cast<Bridge*>(c.state.get());
            if (!api->controls || bridge.stage != Bridge::Stage::Running)
                return;
            char error[1024]{};
            if (!api->controls(&bridge.host, error, sizeof(error))) {
                error[1023] = 0;
                throw std::runtime_error(std::string("Native control callback failed: ") + error);
            }
        };
        if (api->scene_ready)
            result.scene_ready = [api](ModuleContext& c) {
                auto& bridge = *static_cast<Bridge*>(c.state.get());
                bridge.stage = Bridge::Stage::Preparing;
                char error[1024]{};
                try {
                    if (!api->scene_ready(&bridge.host, error, sizeof(error))) {
                        error[1023] = 0;
                        throw std::runtime_error(
                            std::string("Native scene initialization failed: ") + error);
                    }
                } catch (...) {
                    bridge.stage = Bridge::Stage::Running;
                    throw;
                }
                bridge.stage = Bridge::Stage::Running;
            };
        if (api->restore)
            result.restore = [api](ModuleContext& c, const Json& state) {
                auto& bridge = *static_cast<Bridge*>(c.state.get());
                const auto text = state.dump();
                char error[1024]{};
                if (bridge.stage != Bridge::Stage::Running)
                    throw std::runtime_error("Saved-state restoration requires a started module");
                bridge.stage = Bridge::Stage::Preparing;
                try {
                    if (!api->restore(&bridge.host, text.c_str(), error, sizeof(error))) {
                        error[1023] = 0;
                        throw std::runtime_error(
                            std::string("Native saved-state restoration failed: ") + error);
                    }
                } catch (...) {
                    bridge.stage = Bridge::Stage::Running;
                    throw;
                }
                bridge.stage = Bridge::Stage::Running;
            };
        result.stop = [api](ModuleContext& c) {
            if (c.state) {
                auto& bridge = *static_cast<Bridge*>(c.state.get());
                bridge.release_entities();
                bridge.release_resources();
                if (bridge.context.services.available(Capability::Game))
                    bridge.context.services.game()->revoke(bridge.context.id);
                bridge.game_requests.clear();
                bridge.ui_events.clear();
                bridge.stage = Bridge::Stage::Stopped;
            }
            if (api->stop && c.state)
                api->stop(&static_cast<Bridge*>(c.state.get())->host);
        };
        return result;
#endif
    } catch (const std::exception& e) {
        if (services.available(Capability::Diagnostics)) {
            Diagnostic d{Severity::Error, "module", e.what(), {}};
            d.context.module = id;
            d.context.source = path_utf8(path);
            services.emit(std::move(d));
        }
        throw;
    }
}
std::vector<EngineModule> project_native_modules(const std::filesystem::path& root,
                                                 const nlohmann::json& project,
                                                 ServiceAccess services) {
    validate_project_modules(project);
    std::vector<EngineModule> result;
    for (const auto& m : project.value("modules", nlohmann::json::array()))
        if (m.is_object()) {
            if (m.at("fingerprint") != FORGE_NATIVE_SDK_FINGERPRINT)
                throw std::runtime_error("Project module SDK fingerprint mismatch: " +
                                         m.at("id").get<std::string>());
            auto module = load_native_sdk(ProjectPaths(root).resolve(std::filesystem::u8path(
                                              m.at("library").get<std::string>())),
                                          m.at("id"), m.at("implementation"), services);
            if (module.dependencies != m.at("dependencies").get<std::vector<std::string>>())
                throw std::runtime_error("Module dependency declaration does not match library");
            result.push_back(std::move(module));
        }
    return result;
}
} // namespace forge
