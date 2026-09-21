#include "runtime_entity_creation.hpp"
#include <algorithm>
#include <atomic>
#include <forge/scene.hpp>
#include <limits>
#include <map>
#include <thread>
namespace forge::detail {
namespace {
// Borrowed owner on the existing SceneMember target. Destruction of that target
// retires this pointer. It is never reflected, inherited or serialized.
struct RuntimeSceneOwner {
    Scene* scene = nullptr;
};
struct Request {
    std::string module, name;
    AssetId target;
    RuntimeEntityResult result;
    flecs::entity_t membership = 0;
};
struct RuntimeEntityQueue {
    std::map<std::uint64_t, Request> requests;
};
struct RuntimeEntityHost {
    std::shared_ptr<RuntimeEntityQueue> queue;
};
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
    if (context.role() == WorldRole::Runtime)
        world.set<RuntimeEntityHost>({std::make_shared<RuntimeEntityQueue>()});
}
void attach_runtime_scene(Scene& scene) {
    scene.world().entity(scene.membership()).set<RuntimeSceneOwner>({&scene});
}
std::uint64_t request_runtime_entity(WorldContext& context, const std::string& module,
                                     AssetId scene, const std::string& name) {
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
    values.requests.emplace(token, Request{module, name, scene, std::move(pending), 0});
    return token;
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
            auto candidate = selected->document();
            const auto id = result.reference.entity.str();
            candidate["entities"].push_back(
                {{"id", id}, {"name", intended.name}, {"components", Json::object()}});
            // Existing candidate validation and differential native realization.
            // Only the empty new entity is introduced; gameplay sets its native
            // components through normal Flecs APIs after observing Ready.
            selected->replace(candidate);
            result.reference = selected->reference(id);
            result.native_entity = selected->entity(id).id();
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
} // namespace forge::detail
