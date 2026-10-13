#include "builtins.hpp"
#include "navigation_asset.hpp"
#include "navigation_geometry.hpp"
#include <algorithm>
#include <cmath>
#include <forge/assets.hpp>
#include <forge/navigation.hpp>
#include <forge/physics_components.hpp>
#include <forge/scene.hpp>
#include <set>
#include <thread>
namespace forge {
namespace {
using namespace navigation_detail;
struct NavigationFailure : std::runtime_error {
    NavStatus status;
    NavigationFailure(NavStatus s, std::string why)
        : std::runtime_error(std::move(why)), status(s) {}
};
const detail::Builtin& agent_descriptor() {
    for (const auto& type : detail::builtins())
        if (std::string_view(type.name) == "forge.navigation_agent")
            return type;
    throw std::logic_error("Navigation schema is missing");
}
struct Cached {
    Admitted data;
    std::string revision;
    std::unique_ptr<Query> query;
};
struct AgentState {
    NavigationAgent config;
    std::vector<Double3> path;
    std::size_t next = 1;
    Double3 last{};
    NavigationAgentObservation observation;
};
double distance(Double3 a, Double3 b) { return std::hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]); }
double route_distance(Double3 position, const AgentState& state) {
    if (state.path.empty())
        return 0;
    if (state.next >= state.path.size())
        return distance(position, state.path.back());
    auto value = distance(position, state.path[state.next]);
    for (auto i = state.next + 1; i < state.path.size(); ++i)
        value += distance(state.path[i - 1], state.path[i]);
    return value;
}
constexpr double arrival_tolerance = .001;
} // namespace
const char* navigation_agent_phase_name(NavigationAgentPhase phase) {
    constexpr const char* names[]{"Disabled", "Idle",    "Pending", "Moving",
                                  "Paused",   "Arrived", "Blocked", "Failed"};
    const auto i = unsigned(phase);
    return i < std::size(names) ? names[i] : "Invalid";
}
struct NavigationRuntime::Impl {
    WorldContext& context;
    std::filesystem::path project;
    Scene* scene = nullptr;
    std::thread::id owner = std::this_thread::get_id();
    std::map<AssetId, Cached> cache;
    std::map<flecs::entity_t, AgentState> agents;
    std::map<flecs::entity_t, std::string> errors;
    std::uint64_t tick = 0;
    bool agent_limit = false;
    std::string geometry_digest, geometry_error;
    std::string geometry_v2;
    NavStatus geometry_status = NavStatus::Stale;
    bool geometry_loading = false;
    std::weak_ptr<RuntimeResourceService> mesh_owner;
    std::map<AssetId, std::uint64_t> mesh_tokens;
    flecs::query<const NavigationSurface> surfaces;
    Impl(WorldContext& c, std::filesystem::path p)
        : context(c), project(std::move(p)), surfaces(c.world().query<const NavigationSurface>()) {}
    void release_meshes() noexcept {
        if (auto resources = mesh_owner.lock())
            for (const auto& [id, token] : mesh_tokens)
                try {
                    resources->release(token);
                } catch (...) { /* provider may already be retired */
                }
        mesh_tokens.clear();
        mesh_owner.reset();
    }
    ~Impl() { release_meshes(); }
    void check() const {
        if (owner != std::this_thread::get_id() || !scene)
            throw NavigationFailure(NavStatus::Unavailable,
                                    "Navigation requires its bound runtime owner thread");
    }
    void diagnostic(flecs::entity_t entity, AssetId asset, const std::string& text) {
        if (errors[entity] == text)
            return;
        errors[entity] = text;
        auto services = context.services();
        if (!services.available(Capability::Diagnostics))
            return;
        DiagnosticContext dc;
        auto ref = context.reference(entity);
        if (ref)
            dc.entity = ref->entity;
        if (asset)
            dc.asset = asset;
        dc.module = "forge.navigation";
        dc.world_role = world_role_name(context.role());
        dc.tick = tick;
        dc.source = "NavMeshAsset";
        services.emit({Severity::Warning, "navigation", text, dc});
    }
    Cached& get(AssetRef<NavMeshAsset> ref) {
        check();
        if (!ref.id)
            throw NavigationFailure(NavStatus::Missing, "Choose a NavMesh asset");
        if (!cache.contains(ref.id)) {
            auto profile = context.services().profile("navigation", "NavLoad", tick);
            auto catalog = AssetCatalog::open_project(project);
            auto found = catalog.resolve(ref);
            if (found.state != AssetState::Available)
                throw NavigationFailure(NavStatus::Missing, found.diagnostic);
            if (cache.size() >= 8)
                throw NavigationFailure(
                    NavStatus::Limit,
                    "Navigation world cache exceeds eight assets / 32 MiB tile data");
            auto loaded = load(project, *found.record);
            auto query = std::make_unique<Query>(loaded.mesh);
            cache.emplace(ref.id, Cached{std::move(loaded), found.record->metadata.at("sha256"),
                                         std::move(query)});
        }
        auto& value = cache.at(ref.id);
        if (value.data.metadata.at("source_scene").get<AssetId>() != scene->asset_id())
            throw NavigationFailure(NavStatus::Invalid,
                                    "NavMesh was built for another scene asset");
        const auto& digest = value.data.metadata.at("version") == 1 ? geometry_digest : geometry_v2;
        if (!digest.empty() &&
            digest != value.data.metadata.at("geometry_sha256").get<std::string>())
            throw NavigationFailure(
                NavStatus::Stale, "Navigation geometry changed; stop Play and rebuild navigation");
        if (!geometry_error.empty())
            throw NavigationFailure(geometry_status, geometry_error);
        if (digest.empty())
            throw NavigationFailure(NavStatus::Stale, "Navigation geometry provenance unavailable");
        return value;
    }
    void valid_agent(flecs::entity e, const NavigationAgent& a,
                     const std::map<std::uint64_t, TransformNode>& nodes) {
        try {
            detail::validate_reflected_value(e, a);
        } catch (const std::exception& error) {
            throw NavigationFailure(NavStatus::Invalid, error.what());
        }
        if (e.has<PhysicsBody>())
            throw NavigationFailure(NavStatus::Invalid,
                                    "NavigationAgent movement requires a nonphysics entity");
        if (e.has<NavigationSurface>() && e.get<NavigationSurface>().enabled)
            throw NavigationFailure(
                NavStatus::Invalid,
                "A moving NavigationAgent cannot be baked NavigationSurface geometry");
        if (!nodes.contains(e.id()) || !e.has<WorldTransform>() ||
            !e.get<WorldTransform>().resolved)
            throw NavigationFailure(NavStatus::Invalid,
                                    "NavigationAgent requires a resolved spatial transform");
        std::set<std::uint64_t> seen;
        auto current = nodes.at(e.id()).parent;
        while (current) {
            if (!seen.insert(current).second || !nodes.contains(current))
                throw NavigationFailure(NavStatus::Invalid,
                                        "Unresolved navigation spatial ancestry");
            auto ancestor = context.world().entity(current);
            if (ancestor.has<PhysicsBody>() && ancestor.get<PhysicsBody>().motion != 0)
                throw NavigationFailure(
                    NavStatus::Invalid,
                    "NavigationAgent cannot follow a moving physics ancestor: " +
                        std::to_string(current));
            current = nodes.at(current).parent;
        }
    }
};
NavigationRuntime::NavigationRuntime(WorldContext& c, std::filesystem::path p)
    : impl_(std::make_unique<Impl>(c, std::move(p))) {}
NavigationRuntime::~NavigationRuntime() = default;
void NavigationRuntime::shutdown() noexcept { impl_.reset(); }
void NavigationRuntime::bind(Scene* scene) {
    if (impl_) {
        impl_->scene = scene;
        impl_->agents.clear();
        impl_->errors.clear();
        impl_->agent_limit = false;
        impl_->release_meshes();
    }
}
void NavigationRuntime::synchronize() {
    if (!impl_)
        return;
    auto& s = *impl_;
    s.check();
    if (s.context.world().is_readonly())
        throw std::logic_error("Navigation synchronization requires a writable boundary");
    // The native query follows effective inherited values and excludes templates.
    // With no enabled surface there is no geometry to serialize or hash. In
    // particular unrelated large opaque scene data must not be copied each tick.
    bool has_surface = false;
    s.surfaces.each([&](const NavigationSurface& surface) { has_surface |= surface.enabled; });
    if (!has_surface) {
        s.release_meshes();
        s.geometry_digest.clear();
        s.geometry_v2.clear();
        s.geometry_status = NavStatus::Stale;
        s.geometry_loading = false;
        s.geometry_error = "No enabled NavigationSurface geometry";
        return;
    }
    const auto previous_legacy = std::move(s.geometry_digest);
    const auto previous_sources = std::move(s.geometry_v2);
    s.geometry_digest.clear();
    s.geometry_v2.clear();
    s.geometry_loading = false;
    s.geometry_status = NavStatus::Stale;
    try {
        const auto doc = s.scene->effective_document();
        const auto source = capture(doc);
        std::shared_ptr<RuntimeResourceService> resources;
        if (!source.meshes.empty()) {
            if (!s.context.services().available(Capability::Resources)) {
                s.release_meshes();
                s.geometry_status = NavStatus::Unavailable;
                throw std::runtime_error("Static Mesh navigation requires runtime CPU Resources");
            }
            resources = s.context.services().resources();
            if (s.mesh_owner.lock() != resources) {
                s.release_meshes();
                s.mesh_owner = resources;
            }
        }
        for (auto it = s.mesh_tokens.begin(); it != s.mesh_tokens.end();) {
            if (!source.meshes.contains(it->first)) {
                if (auto owner = s.mesh_owner.lock())
                    owner->release(it->second);
                it = s.mesh_tokens.erase(it);
            } else
                ++it;
        }
        for (auto id : source.meshes)
            if (!s.mesh_tokens.contains(id))
                s.mesh_tokens.emplace(id, resources->request(RuntimeResourceKind::Mesh, id));
        bool ready = true;
        std::uint64_t retained_bytes = 0;
        const auto inputs = mesh_inputs(source, [&](AssetId id) {
            const auto token = s.mesh_tokens.at(id);
            const auto status = resources->inspect(token);
            if (status.requested_revision.empty())
                throw std::runtime_error(status.diagnostic);
            const auto lease = resources->mesh(token);
            if (status.state == "failed")
                throw std::runtime_error("Navigation Mesh admission failed: " + status.diagnostic);
            ready &= status.state == "ready" && lease &&
                     status.retained_revision == status.requested_revision;
            if (lease)
                retained_bytes += lease->resident_bytes();
            return status.requested_revision;
        });
        if (retained_bytes > mesh_bytes) {
            s.geometry_status = NavStatus::Limit;
            throw std::runtime_error("Navigation Mesh inputs exceed 64 MiB retained CPU data");
        }
        s.geometry_v2 = source_digest(source, inputs);
        if (source.meshes.empty())
            s.geometry_digest = !previous_legacy.empty() && previous_sources == s.geometry_v2
                                    ? previous_legacy
                                    : navigation_geometry_digest(doc);
        if (!ready) {
            s.geometry_loading = true;
            s.geometry_status = NavStatus::Unavailable;
            throw std::runtime_error("Waiting for selected navigation Mesh resources");
        }
        s.geometry_error.clear();
    } catch (const std::exception& e) {
        s.geometry_digest.clear();
        s.geometry_error = e.what();
    }
}
NavResult NavigationRuntime::project_point(AssetRef<NavMeshAsset> ref, Double3 p) {
    if (!impl_)
        return {NavStatus::Unavailable, {}, "Navigation stopped"};
    try {
        impl_->check();
        if (!impl_->context.world().is_readonly())
            synchronize();
        auto scope = impl_->context.services().profile("navigation", "NavQuery", impl_->tick);
        return impl_->get(ref).query->project(p);
    } catch (const NavigationFailure& e) {
        return {e.status, {}, e.what()};
    } catch (const std::exception& e) {
        return {NavStatus::Invalid, {}, e.what()};
    }
}
NavResult NavigationRuntime::find_path(AssetRef<NavMeshAsset> ref, Double3 a, Double3 b) {
    if (!impl_)
        return {NavStatus::Unavailable, {}, "Navigation stopped"};
    try {
        impl_->check();
        if (!impl_->context.world().is_readonly())
            synchronize();
        auto scope = impl_->context.services().profile("navigation", "NavQuery", impl_->tick);
        return impl_->get(ref).query->path(a, b);
    } catch (const NavigationFailure& e) {
        return {e.status, {}, e.what()};
    } catch (const std::exception& e) {
        return {NavStatus::Invalid, {}, e.what()};
    }
}
void NavigationRuntime::tick(float dt, std::uint64_t tick) {
    if (!impl_)
        return;
    auto& s = *impl_;
    s.check();
    s.tick = tick;
    synchronize();
    if (!std::isfinite(dt) || dt <= 0 || dt > 1)
        throw std::runtime_error("Navigation requires positive bounded fixed delta");
    auto profile = s.context.services().profile("navigation", "NavAgentUpdate", tick);
    s.context.evaluate_world_transforms();
    auto nodes = s.context.transform_nodes();
    std::vector<flecs::entity_t> entities;
    s.context.world().each([&](flecs::entity e, const NavigationAgent&) {
        if (e.owns<PersistentEntityId>())
            entities.push_back(e.id());
    });
    std::set<flecs::entity_t> alive(entities.begin(), entities.end());
    std::erase_if(s.agents, [&](const auto& p) { return !alive.contains(p.first); });
    std::erase_if(s.errors, [&](const auto& p) { return p.first && !alive.contains(p.first); });
    if (entities.size() > 64) {
        s.diagnostic(0, {}, "Navigation exceeds 64 authored agents; no agents moved");
        s.agents.clear();
        s.agent_limit = true;
        return;
    }
    s.agent_limit = false;
    auto depth = [&](std::uint64_t id) {
        unsigned n = 0;
        std::set<std::uint64_t> seen;
        while (nodes.contains(id) && nodes.at(id).parent && seen.insert(id).second) {
            ++n;
            id = nodes.at(id).parent;
        }
        return n;
    };
    std::stable_sort(entities.begin(), entities.end(),
                     [&](auto a, auto b) { return depth(a) < depth(b); });
    for (auto id : entities) {
        auto e = s.context.world().entity(id);
        auto a = e.get<NavigationAgent>();
        if (!a.enabled || !a.has_destination) {
            s.agents.erase(id);
            s.errors.erase(id);
            continue;
        }
        try {
            s.context.evaluate_world_transforms();
            s.valid_agent(e, a, nodes);
            auto& asset = s.get(a.navmesh);
            auto position = e.get<WorldTransform>().affine.point({0, 0, 0});
            Double3 destination{a.destination_x, a.destination_y, a.destination_z};
            auto& state = s.agents[id];
            if (state.config != a || state.path.empty() || distance(state.last, position) > .001) {
                auto route = asset.query->path(position, destination);
                state = {a, std::move(route.points), 1, position, {}};
                if (route.status != NavStatus::Success)
                    throw NavigationFailure(route.status, route.diagnostic);
            }
            if (state.path.empty())
                continue;
            double remaining = double(a.speed) * dt;
            const auto original = position;
            while (remaining > 0 && state.next < state.path.size()) {
                const auto distance_left = route_distance(position, state);
                if (distance_left <= a.stopping_distance + arrival_tolerance)
                    break;
                auto waypoint = state.path[state.next];
                double d = distance(position, waypoint);
                if (d < 1e-6) {
                    ++state.next;
                    continue;
                }
                const auto advance =
                    std::min({remaining, d, std::max(0., distance_left - a.stopping_distance)});
                if (advance < 1e-8)
                    break;
                Double3 desired;
                for (unsigned j = 0; j < 3; ++j)
                    desired[j] = position[j] + (waypoint[j] - position[j]) * (advance / d);
                auto moved = asset.query->move(position, desired);
                if (moved.status != NavStatus::Success)
                    throw NavigationFailure(moved.status, moved.diagnostic);
                auto next = moved.points.front();
                if (distance(next, position) < 1e-8)
                    break;
                position = next;
                remaining -= advance;
                if (distance(position, waypoint) < .001)
                    ++state.next;
            }
            if (distance(position, original) > 1e-8) {
                auto local = position;
                auto parent = nodes.at(id).parent;
                if (parent) {
                    auto p = s.context.world().entity(parent);
                    if (!p.has<WorldTransform>() || !p.get<WorldTransform>().resolved)
                        throw NavigationFailure(NavStatus::Invalid,
                                                "Navigation spatial parent unavailable");
                    local = inverse(p.get<WorldTransform>().affine).point(position);
                }
                e.set<LocalTranslation>({local[0], local[1], local[2]});
            }
            state.last = position;
            auto& out = state.observation;
            out = {};
            out.tick = tick;
            out.sampled = true;
            out.has_path = true;
            out.status = NavStatus::Success;
            out.position = position;
            out.projected_destination = state.path.back();
            out.remaining_distance = route_distance(position, state);
            for (unsigned j = 0; j < 3; ++j)
                out.velocity[j] = (position[j] - original[j]) / dt;
            out.phase = out.remaining_distance <= a.stopping_distance + arrival_tolerance
                            ? NavigationAgentPhase::Arrived
                        : a.speed == 0                        ? NavigationAgentPhase::Paused
                        : distance(position, original) > 1e-8 ? NavigationAgentPhase::Moving
                                                              : NavigationAgentPhase::Blocked;
            s.errors.erase(id);
        } catch (const std::exception& error) {
            auto& state = s.agents[id];
            state = {};
            state.config = a;
            const auto* failure = dynamic_cast<const NavigationFailure*>(&error);
            const bool pending =
                s.geometry_loading && failure && failure->status == NavStatus::Unavailable;
            state.observation.phase =
                pending ? NavigationAgentPhase::Pending : NavigationAgentPhase::Failed;
            state.observation.status = failure ? failure->status : NavStatus::Invalid;
            state.observation.tick = pending ? 0 : tick;
            // A failed movement tick has no valid path/pose/velocity sample.
            if (!pending)
                s.diagnostic(id, a.navmesh.id, error.what());
        }
    }
}
std::optional<NavigationAgentObservation> NavigationRuntime::agent_state(EntityRef ref) const {
    if (!impl_ || !impl_->scene || !ref.scene || !ref.entity ||
        impl_->owner != std::this_thread::get_id())
        return {};
    auto& s = *impl_;
    const auto resolved = s.context.resolve(ref);
    if (resolved.state != WorldContext::ResolveState::Available)
        return {};
    const auto entity = s.context.world().entity(resolved.entity);
    if (!entity.has<NavigationAgent>() || !entity.owns<PersistentEntityId>())
        return {};
    const auto& config = entity.get<NavigationAgent>();
    NavigationAgentObservation out;
    if (!config.enabled)
        out.phase = NavigationAgentPhase::Disabled;
    else if (!config.has_destination)
        out.phase = NavigationAgentPhase::Idle;
    else if (s.agent_limit) {
        out.phase = NavigationAgentPhase::Failed;
        out.status = NavStatus::Limit;
        out.tick = s.tick;
    } else if (const auto found = s.agents.find(entity.id());
               found != s.agents.end() && found->second.config == config)
        return found->second.observation;
    return out;
}
Json NavigationRuntime::debug(flecs::entity_t entity) const {
    if (!entity || !impl_ || impl_->owner != std::this_thread::get_id())
        return nullptr;
    const auto reference = impl_->context.reference(entity);
    const auto out = reference ? agent_state(*reference) : std::nullopt;
    if (!out)
        return nullptr;
    Json path = Json::array();
    if (out->has_path)
        path = impl_->agents.at(entity).path;
    return {{"path", path},
            {"status", nav_status_name(out->status)},
            {"phase", navigation_agent_phase_name(out->phase)},
            {"tick", out->tick},
            {"sampled", out->sampled},
            {"has_path", out->has_path},
            {"position", out->position},
            {"velocity", out->velocity},
            {"projected_destination", out->projected_destination},
            {"remaining_distance", out->remaining_distance}};
}
Json NavigationRuntime::checkpoint() {
    Json revisions = Json::array(), agents = Json::array();
    if (impl_) {
        impl_->check();
        impl_->context.evaluate_world_transforms();
        const auto nodes = impl_->context.transform_nodes();
        for (const auto& [id, c] : impl_->cache)
            revisions.push_back({{"asset", id}, {"sha256", c.revision}});
        impl_->context.world().each([&](flecs::entity e, const NavigationAgent&) {
            auto ref = impl_->context.reference(e.id());
            if (!ref)
                return;
            if (agents.size() >= 64)
                throw std::runtime_error("Navigation checkpoint exceeds 64 agents");
            auto config = agent_descriptor().read(e, true);
            detail::validate_components({{"forge.navigation_agent", config}});
            Json translation = nullptr;
            try {
                impl_->valid_agent(e, e.get<NavigationAgent>(), nodes);
                auto t = e.get<LocalTranslation>();
                translation = {t.x, t.y, t.z};
            } catch (const NavigationFailure&) { /* Unsupported physics/spatial configurations are
                                                    not navigation transform authority. */
            }
            agents.push_back(
                {{"entity", *ref},
                 {"config", config},
                 {"owned", e.owns<NavigationAgent>()},
                 {"translation", translation},
                 {"translation_owned", !translation.is_null() && e.owns<LocalTranslation>()}});
        });
    }
    return {{"version", 1}, {"assets", revisions}, {"agents", agents}};
}
void NavigationRuntime::restore(const Json& data) {
    if (!impl_ || data.at("version") != 1 || !data.at("assets").is_array() ||
        data.at("assets").size() > 8 || !data.at("agents").is_array() ||
        data.at("agents").size() > 64)
        throw std::runtime_error("Invalid navigation recovery metadata");
    impl_->check();
    struct Pending {
        flecs::entity entity;
        NavigationAgent agent;
        std::optional<LocalTranslation> translation;
        bool owned, translation_owned;
    };
    std::vector<Pending> pending;
    std::set<flecs::entity_t> expected, seen_entities;
    impl_->context.world().each([&](flecs::entity e, const NavigationAgent&) {
        if (impl_->context.reference(e.id()))
            expected.insert(e.id());
    });
    for (const auto& entry : data.at("agents")) {
        auto found = impl_->context.resolve(entry.at("entity").get<EntityRef>());
        if (found.state != WorldContext::ResolveState::Available ||
            !expected.contains(found.entity) || !seen_entities.insert(found.entity).second)
            throw std::runtime_error("Navigation recovery agent identity differs");
        auto e = impl_->context.world().entity(found.entity);
        detail::validate_components({{"forge.navigation_agent", entry.at("config")}});
        auto config = std::get<NavigationAgent>(agent_descriptor().decode(entry.at("config")));
        bool owned = entry.at("owned").get<bool>(),
             translation_owned = entry.at("translation_owned").get<bool>();
        std::optional<LocalTranslation> translation;
        if (!entry.at("translation").is_null()) {
            auto xyz = entry.at("translation").get<Double3>();
            for (auto v : xyz)
                if (!std::isfinite(v))
                    throw std::runtime_error("Invalid navigation recovery translation");
            translation = LocalTranslation{xyz[0], xyz[1], xyz[2]};
        }
        if (owned != e.owns<NavigationAgent>() ||
            (translation &&
             (translation_owned != e.owns<LocalTranslation>() || !e.has<LocalTranslation>())) ||
            (!translation && translation_owned) || (!owned && config != e.get<NavigationAgent>()) ||
            (!translation_owned && translation && *translation != e.get<LocalTranslation>()))
            throw std::runtime_error("Navigation recovery component ownership/inheritance differs");
        if (translation) {
            impl_->context.evaluate_world_transforms();
            impl_->valid_agent(e, config, impl_->context.transform_nodes());
        }
        pending.push_back({e, config, translation, owned, translation_owned});
    }
    if (expected != seen_entities)
        throw std::runtime_error("Navigation recovery agent set differs");
    // Host-only recovery into an unpublished candidate world. Preserve component ownership;
    // semantic values cover direct gameplay edits hidden by authored property-override intent.
    for (const auto& p : pending) {
        if (p.owned)
            p.entity.set<NavigationAgent>(p.agent);
        if (p.translation_owned)
            p.entity.set<LocalTranslation>(*p.translation);
    }
    synchronize();
    std::set<AssetId> seen;
    for (const auto& entry : data.at("assets")) {
        auto id = entry.at("asset").get<AssetId>();
        if (!seen.insert(id).second ||
            impl_->get({id}).revision != entry.at("sha256").get<std::string>())
            throw std::runtime_error("Navigation recovery revision differs; start clean Play");
    }
    impl_->agents.clear();
    impl_->agent_limit = false;
    impl_->errors.clear();
}
EngineModule navigation_module(std::filesystem::path project) {
    auto m = navigation_schema_module();
    m.runtime_roles = role_mask(WorldRole::Runtime);
    m.allowed_services = capability(Capability::Diagnostics) | capability(Capability::Profiling) |
                         capability(Capability::Navigation) | capability(Capability::Resources);
    m.provided_services = capability(Capability::Navigation);
    m.start = [project = std::move(project)](ModuleContext& c) {
        auto runtime = std::make_shared<NavigationRuntime>(*c.owner, project);
        c.services.publish_navigation(runtime);
        c.state = runtime;
    };
    m.stop = [](ModuleContext& c) {
        std::static_pointer_cast<NavigationRuntime>(c.state)->shutdown();
        c.services.publish_navigation({});
    };
    return m;
}
} // namespace forge
