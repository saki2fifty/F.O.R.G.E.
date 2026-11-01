#include "runtime_entity_creation.hpp"
#include "scene_draft.hpp"
#include <algorithm>
#include <atomic>
#include <forge/application_states.hpp>
#include <forge/scene.hpp>
#include <limits>
#include <map>
#include <set>
#include <thread>
#include <vector>
namespace forge::detail {
namespace {
// Borrowed owner on the existing SceneMember target. Destruction of that target
// retires this pointer. It is never reflected, inherited or serialized.
struct RuntimeSceneOwner {
    Scene* scene = nullptr;
};
struct Request {
    std::string module, name;
    AssetId target, prefab;
    RuntimeEntityResult result;
    flecs::entity_t membership = 0;
};
struct RuntimeEntityQueue {
    std::map<std::uint64_t, Request> requests;
};
struct RuntimeEntityHost {
    std::shared_ptr<RuntimeEntityQueue> queue;
};
struct RuntimeCreatedRoot {};
RuntimeEntityQueue& queue(WorldContext& context) {
    if (!context.on_owner_thread())
        throw std::runtime_error("Runtime entity creation requires the world owner thread");
    if (context.role() != WorldRole::Runtime)
        throw std::runtime_error("Runtime entity creation requires a runtime world");
    // Read-only singleton access is valid from ordinary deferred gameplay
    // systems. Only its owner-bound engine queue changes; no Flecs mutation.
    return *context.world().get<RuntimeEntityHost>().queue;
}
Request& owned(RuntimeEntityQueue& values, const std::string& module, std::uint64_t token) {
    auto found = values.requests.find(token);
    if (found == values.requests.end() || found->second.module != module)
        throw std::runtime_error("Unknown, released or foreign runtime entity request");
    return found->second;
}
} // namespace
void register_runtime_entity_creation(WorldContext& context) {
    auto& world = context.world();
    world.component<RuntimeSceneOwner>("forge.runtime.SceneOwner")
        .add(flecs::OnInstantiate, flecs::DontInherit);
    world.component<RuntimeEntityHost>("forge.runtime.EntityRequests")
        .add(flecs::OnInstantiate, flecs::DontInherit);
    world.component<RuntimeCreatedRoot>("forge.runtime.CreatedRoot")
        .add(flecs::OnInstantiate, flecs::DontInherit);
    if (context.role() == WorldRole::Runtime)
        world.set<RuntimeEntityHost>({std::make_shared<RuntimeEntityQueue>()});
}
void attach_runtime_scene(Scene& scene) {
    scene.world().entity(scene.membership()).set<RuntimeSceneOwner>({&scene});
}
static std::uint64_t request_runtime_creation(WorldContext& context, const std::string& module,
                                              AssetId scene, AssetId prefab,
                                              const std::string& name) {
    auto& values = queue(context);
    if (!valid_module_id(module) || name.empty() || name.size() > 255 ||
        name.find('\0') != std::string::npos)
        throw std::runtime_error("Runtime entity requires a valid module and 1..255-byte name");
    (void)Json(name).dump(); // Strict UTF-8 check before admission or world mutation.
    if (values.requests.size() >= 256 ||
        std::count_if(values.requests.begin(), values.requests.end(),
                      [&](const auto& item) { return item.second.module == module; }) >= 64)
        throw std::runtime_error("Runtime entity request limit reached (64/module, 256/world)");
    static std::atomic<std::uint64_t> next{1};
    auto token = next.load();
    do {
        if (token == std::numeric_limits<std::uint64_t>::max())
            throw std::runtime_error("Runtime entity request token space exhausted");
    } while (!next.compare_exchange_weak(token, token + 1));
    RuntimeEntityResult pending;
    pending.reference = {scene, EntityId::generate()};
    values.requests.emplace(token, Request{module, name, scene, prefab, std::move(pending), 0});
    return token;
}
std::uint64_t request_runtime_entity(WorldContext& context, const std::string& module,
                                     AssetId scene, const std::string& name) {
    return request_runtime_creation(context, module, scene, {}, name);
}
std::uint64_t request_runtime_prefab(WorldContext& context, const std::string& module,
                                     AssetId scene, AssetId prefab, const std::string& name) {
    if (!prefab)
        throw std::runtime_error("Runtime prefab requires an asset identity");
    return request_runtime_creation(context, module, scene, prefab, name);
}
RuntimeEntityResult inspect_runtime_entity(WorldContext& context, const std::string& module,
                                           std::uint64_t token) {
    auto result = owned(queue(context), module, token).result;
    if (result.state == RuntimeEntityState::Ready) {
        const auto& request = owned(queue(context), module, token);
        const auto target = context.resolve(result.reference, request.membership);
        if (target.state != WorldContext::ResolveState::Available ||
            target.entity != result.native_entity) {
            result.state = RuntimeEntityState::Gone;
            result.native_entity = 0;
            result.diagnostic = "Created runtime entity or its scene is no longer loaded";
        }
    }
    return result;
}
bool release_runtime_entity(WorldContext& context, const std::string& module, std::uint64_t token) {
    auto& values = queue(context);
    auto found = values.requests.find(token);
    if (found == values.requests.end() || found->second.module != module)
        return false;
    // Before publication this cancels the request. Afterwards only the observer
    // is released; ordinary Flecs/Scene lifetime owns the created entity.
    values.requests.erase(found);
    return true;
}
void publish_runtime_entities(WorldContext& context) {
    auto& values = queue(context);
    if (ecs_is_deferred(context.world()) || ecs_stage_is_readonly(context.world()))
        throw std::runtime_error("Runtime entities publish only outside Flecs progress/defer");
    std::vector<std::uint64_t> pending;
    for (const auto& [token, request] : values.requests)
        if (request.result.state == RuntimeEntityState::Pending)
            pending.push_back(token);
    // Fixed bounded snapshot: callbacks cannot extend this publication batch.
    std::size_t remaining_rows = 4096;
    for (const auto token : pending) {
        auto found = values.requests.find(token);
        if (found == values.requests.end())
            continue;
        const auto intended = found->second;
        auto result = intended.result;
        flecs::entity_t membership = 0;
        try {
            Scene* selected = nullptr;
            unsigned matches = 0;
            context.world().each([&](const RuntimeSceneOwner& owner) {
                if (owner.scene &&
                    (!intended.target || owner.scene->asset_id() == intended.target)) {
                    selected = owner.scene;
                    ++matches;
                }
            });
            if (matches != 1)
                throw std::runtime_error(matches ? "Runtime scene selection is ambiguous"
                                                 : "Runtime scene is not loaded");
            std::size_t rows = 1;
            if (intended.prefab) {
                const auto source = selected->prefab_sources().find(intended.prefab);
                if (source == selected->prefab_sources().end())
                    throw std::runtime_error(
                        "Runtime prefab is not admitted; review Runtime Dependencies");
                rows = source->second.at("members").size();
            }
            if (rows > remaining_rows)
                break; // Preserve order and bound one fixed boundary's work.
            remaining_rows -= rows;
            const auto id = result.reference.entity.str();
            Json candidate;
            if (intended.prefab) {
                SceneDraft draft(*selected);
                draft.instantiate_prefab(intended.prefab, result.reference.entity);
                candidate = draft.document();
                for (auto& row : candidate["entities"])
                    if (row.at("id") == id) {
                        row["name"] = intended.name;
                        row["name_override"] = true;
                    }
            } else {
                candidate = selected->document();
                candidate["entities"].push_back(
                    {{"id", id}, {"name", intended.name}, {"components", Json::object()}});
            }
            // Existing detached validation and differential native realization
            // publish the complete member group without authoring history.
            selected->replace(candidate);
            result.reference = selected->reference(id);
            result.native_entity = selected->entity(id).id();
            context.world().entity(result.native_entity).add<RuntimeCreatedRoot>();
            result.state = RuntimeEntityState::Ready;
            membership = selected->membership();
        } catch (const std::exception& error) {
            result.state = RuntimeEntityState::Failed;
            result.diagnostic = error.what();
        }
        // A trusted observer may have released its token during native creation.
        found = values.requests.find(token);
        if (found != values.requests.end()) {
            found->second.result = std::move(result);
            found->second.membership = membership;
        }
    }
}
nlohmann::json control_runtime_entity_scope(WorldContext& context, const Json& command,
                                            const std::string& module) {
    (void)queue(context); // Existing world/role/thread admission.
    auto& world = context.world();
    if (ecs_is_deferred(world) || ecs_stage_is_readonly(world))
        throw std::runtime_error("Runtime entity lifetime requires the mutable owner boundary");
    if (!command.is_object() || command.size() > 6)
        throw std::runtime_error("Invalid runtime entity lifetime command");
    for (const auto& [key, unused] : command.items()) {
        (void)unused;
        if (key != "operation" && key != "entity" && key != "request" && key != "domain" &&
            key != "state" && key != "expected_revision")
            throw std::runtime_error("Unknown runtime entity lifetime command field");
    }
    if ((command.contains("operation") && command.at("operation") != "entity_state_scope") ||
        command.contains("entity") == command.contains("request"))
        throw std::runtime_error("Select one runtime root reference or owned creation request");
    flecs::entity_t root = 0;
    if (command.contains("request")) {
        if (!command.at("request").is_number_unsigned())
            throw std::runtime_error("Runtime creation request must be an unsigned token");
        const auto result =
            inspect_runtime_entity(context, module, command.at("request").get<std::uint64_t>());
        if (result.state != RuntimeEntityState::Ready)
            throw std::runtime_error("State lifetime requires a Ready runtime creation request");
        root = result.native_entity;
    } else {
        const auto resolved = context.resolve(command.at("entity").get<EntityRef>());
        if (resolved.state != WorldContext::ResolveState::Available)
            throw std::runtime_error("Runtime entity reference is unavailable or ambiguous");
        root = resolved.entity;
    }
    if (!root || !world.entity(root).has<RuntimeCreatedRoot>())
        throw std::runtime_error("State lifetime accepts only runtime-created roots");
    const auto reference = context.reference(root);
    if (!reference)
        throw std::runtime_error("Runtime root has lost its Scene membership");
    std::optional<std::uint64_t> revision;
    if (command.contains("expected_revision")) {
        if (!command.at("expected_revision").is_number_unsigned())
            throw std::runtime_error("State revision must be an unsigned integer");
        revision = command.at("expected_revision").get<std::uint64_t>();
    }
    bind_application_state_lifetime(world, root, command.at("domain").get<std::string>(),
                                    command.at("state").get<std::string>(), revision);
    return {{"entity", *reference}, {"scope", application_state_lifetime(world, root)}};
}
Json checkpoint_runtime_entity_lifetimes(WorldContext& context) {
    (void)queue(context);
    auto& world = context.world();
    if (ecs_is_deferred(world) || ecs_stage_is_readonly(world))
        throw std::runtime_error("Runtime lifetime recovery requires the owner boundary");
    Json rows = Json::array();
    auto query =
        world.query_builder().with<RuntimeCreatedRoot>().query_flags(EcsQueryMatchDisabled).build();
    query.each([&](flecs::entity root) {
        if (rows.size() >= 65536)
            throw std::runtime_error("Runtime lifetime recovery exceeds 65536 roots");
        const auto reference = context.reference(root.id());
        if (!reference)
            throw std::runtime_error("Runtime root has lost its Scene membership");
        rows.push_back(
            {{"entity", *reference}, {"scope", application_state_lifetime(world, root.id())}});
    });
    return {{"version", 1}, {"roots", std::move(rows)}};
}
void restore_runtime_entity_lifetimes(WorldContext& context, const Json& value) {
    (void)queue(context);
    auto& world = context.world();
    if (ecs_is_deferred(world) || ecs_stage_is_readonly(world))
        throw std::runtime_error("Runtime lifetime recovery requires the owner boundary");
    if (value.is_null())
        return; // Older private recovery contained no root markers or bindings.
    if (!value.is_object() || value.size() != 2 || value.at("version") != 1 ||
        !value.at("version").is_number_integer() || !value.at("roots").is_array() ||
        value.at("roots").size() > 65536)
        throw std::runtime_error("Invalid runtime lifetime recovery envelope");
    struct Row {
        flecs::entity_t root;
        Json scope;
    };
    std::vector<Row> rows;
    std::set<flecs::entity_t> seen;
    const auto states = application_states_status(world, true);
    for (const auto& row : value.at("roots")) {
        if (!row.is_object() || row.size() != 2)
            throw std::runtime_error("Invalid runtime lifetime recovery root");
        const auto resolved = context.resolve(row.at("entity").get<EntityRef>());
        if (resolved.state != WorldContext::ResolveState::Available ||
            !seen.insert(resolved.entity).second ||
            world.entity(resolved.entity).has(flecs::Prefab))
            throw std::runtime_error("Invalid, duplicate or missing runtime lifetime root");
        const auto& scope = row.at("scope");
        if (!scope.is_null()) {
            if (!scope.is_object() || scope.size() != 2 || states.is_null())
                throw std::runtime_error("Invalid runtime lifetime recovery scope");
            const auto domain = scope.at("domain").get<std::string>();
            const auto state = scope.at("state").get<std::string>();
            if (!application_state_is(world, domain, state))
                throw std::runtime_error("Recovered lifetime is not an active state or ancestor");
            // A known active key cannot be a schema-only placeholder.
            if (!std::any_of(states.at("domains").begin(), states.at("domains").end(),
                             [&](const auto& d) { return d.at("key") == domain; }))
                throw std::runtime_error("Recovered lifetime domain is not configured");
        }
        if (!application_state_lifetime(world, resolved.entity).is_null())
            throw std::runtime_error("Runtime lifetime recovery requires fresh roots");
        rows.push_back({resolved.entity, scope});
    }
    // Every reference/scope validates before native mutation. Tokens are not restored.
    for (const auto& row : rows) {
        world.entity(row.root).add<RuntimeCreatedRoot>();
        if (!row.scope.is_null())
            bind_application_state_lifetime(world, row.root,
                                            row.scope.at("domain").get<std::string>(),
                                            row.scope.at("state").get<std::string>());
    }
}
} // namespace forge::detail
