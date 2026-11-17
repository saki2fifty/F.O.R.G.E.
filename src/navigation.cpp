#include "builtins.hpp"
#include "json_output_text.hpp"
#include "navigation_asset.hpp"
#include "navigation_behavior_runtime.hpp"
#include "navigation_link_collision.hpp"
#include "navigation_link_curve.hpp"
#include "navigation_obstacle.hpp"
#include "navigation_schema.hpp"
#include "navigation_sources.hpp"
#include "runtime_property_tune.hpp"
#include "runtime_tile_map.hpp"
#include <algorithm>
#include <cmath>
#include <forge/application_state_activation.hpp>
#include <forge/assets.hpp>
#include <forge/character_components.hpp>
#include <forge/navigation.hpp>
#include <forge/physics_components.hpp>
#include <forge/scene.hpp>
#include <limits>
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
    return navigation_descriptor("forge.navigation_agent");
}
struct Cached {
    Admitted data;
    std::string revision;
    std::unique_ptr<Query> query;
    std::optional<std::vector<CylinderObstacle>> obstacle_snapshot;
    std::optional<std::vector<CylinderObstacle>> failed_obstacle_snapshot{};
    std::string obstacle_update_error{};
    std::uint64_t obstacle_attempt = 0;
};
struct AgentState {
    NavigationAgent config;
    std::vector<Double3> path;
    std::size_t next = 1;
    Double3 last{};
    NavigationAgentObservation observation;
    std::uint64_t query_generation = 0;
    std::vector<Query::RouteLink> links;
    std::optional<std::size_t> crossing;
    bool replan_after_link = false;
    bool physical_jump = false;
    struct CharacterFrame {
        Double3 original{};
        bool jump_requested = false;
    };
    std::optional<CharacterFrame> character_frame;
};
const Query::RouteLink* crossing(const AgentState& state) {
    return state.crossing && *state.crossing < state.links.size() ? &state.links[*state.crossing]
                                                                  : nullptr;
}
bool arc_entry(const AgentState& state, std::size_t waypoint) {
    return std::ranges::any_of(state.links, [&](const auto& link) {
        return link.arc_height != 0 && link.entry == waypoint;
    });
}
double retained_pose_tolerance(const AgentState& state) {
    const auto* link = crossing(state);
    return link && link->arc_height != 0 ? 1e-8 : .001;
}
bool begin_crossing(AgentState& state, Double3& position, double& remaining) {
    if (state.crossing)
        return true;
    for (std::size_t i = 0; i < state.links.size(); ++i) {
        const auto& link = state.links[i];
        if ((link.exit == state.next || (link.arc_height != 0 && link.entry == state.next)) &&
            link.entry < state.path.size() &&
            std::hypot(position[0] - state.path[link.entry][0],
                       position[1] - state.path[link.entry][1],
                       position[2] - state.path[link.entry][2]) < .001) {
            if (link.arc_height != 0 && position != state.path[link.entry]) {
                const auto& entry = state.path[link.entry];
                const auto gap = std::hypot(position[0] - entry[0], position[1] - entry[1],
                                            position[2] - entry[2]);
                // Finish the tiny ground approach before deriving arc progress.
                // Projecting a nearby ground pose onto a steep arc would jump.
                if (gap >= 1e-6 || gap > remaining) {
                    state.next = link.entry;
                    return false;
                }
                position = entry;
                remaining -= gap;
            }
            state.next = link.exit;
            state.crossing = i;
            return true;
        }
    }
    return false;
}
double distance(Double3 a, Double3 b) { return std::hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]); }
double route_distance(Double3 position, const AgentState& state) {
    if (state.path.empty())
        return 0;
    if (state.next >= state.path.size())
        return distance(position, state.path.back());
    auto value = distance(position, state.path[state.next]);
    auto segment = state.next;
    for (const auto& link : state.links)
        if (link.arc_height != 0 && link.exit == state.next) {
            const NavigationLinkCurve curve(state.path[link.entry], state.path[link.exit],
                                            link.arc_height);
            value = crossing(state) == &link
                        ? curve.length(std::clamp(curve.parameter(position), 0., 1.))
                        : distance(position, state.path[link.entry]) + curve.length();
            break;
        }
    auto link = state.links.begin();
    for (auto i = segment + 1; i < state.path.size(); ++i) {
        while (link != state.links.end() && link->exit < i)
            ++link;
        if (link != state.links.end() && link->exit == i && link->arc_height != 0)
            value += NavigationLinkCurve(state.path[link->entry], state.path[link->exit],
                                         link->arc_height)
                         .length();
        else
            value += distance(state.path[i - 1], state.path[i]);
    }
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
    std::uint64_t tick = 0, synchronization = 0;
    bool agent_limit = false;
    struct ObstacleSource {
        EntityId id;
        NavigationObstacle component;
        AffineTransform transform;
    };
    std::vector<ObstacleSource> obstacle_sources;
    std::string obstacle_error;
    NavStatus obstacle_status = NavStatus::Invalid;
    struct GeometryState {
        std::string legacy, digest, error;
        NavStatus status = NavStatus::Stale;
        bool loading = false;
        std::uint64_t captured = 0;
        std::optional<NavigationSettings> selection;
        std::set<AssetId> collisions;
    };
    using ProfileKey = std::pair<NavigationPlane, std::string>;
    std::array<GeometryState, 2> geometry;
    std::set<ProfileKey> linked_profiles;
    std::map<ProfileKey, GeometryState> profile_geometry;
    GeometryState& profile_for(const Cached& asset) {
        const auto plane = asset.data.mesh->space();
        const auto settings = asset.data.metadata.at("settings").get<NavigationSettings>();
        const ProfileKey key{plane, settings.profile};
        const auto& floor = geometry.at(unsigned(plane));
        if (!settings.collect_static_colliders &&
            (!linked_profiles.contains(key) || (!floor.error.empty() && floor.digest.empty())))
            return geometry.at(unsigned(plane));
        auto& state = profile_geometry[key];
        if (state.captured != synchronization || state.selection != settings) {
            if (context.world().is_readonly())
                throw NavigationFailure(NavStatus::Unavailable,
                                        "Waiting for navigation profile source synchronization");
            capture_geometry(state, settings);
        }
        return state;
    }
    std::weak_ptr<RuntimeResourceService> source_owner;
    using SourceTokens = std::map<AssetId, std::uint64_t>;
    SourceTokens mesh_tokens, collision_tokens;
    flecs::query<const NavigationSurface> surfaces;
    flecs::query<const NavigationRegion2D> regions;
    flecs::query<const NavigationObstacle> obstacle_entities;
    flecs::query<const NavigationLink> link_entities;
    flecs::query<const PersistentEntityId> source_entities;
    NavigationBehaviorPlayback behaviors;
    bool awaiting_physics() const {
        return behaviors.needs_physics_observation() ||
               std::any_of(agents.begin(), agents.end(), [](const auto& item) {
                   return item.second.character_frame.has_value();
               });
    }
    static Double3 character_route_origin(Cached& asset, Double3 actual) {
        const auto projected = asset.query->project(actual);
        if (projected.status != NavStatus::Success || projected.points.size() != 1)
            throw NavigationFailure(projected.status == NavStatus::Success ? NavStatus::Invalid
                                                                           : projected.status,
                                    "Character has no admitted navigation ground point");
        const auto& ground = projected.points.front();
        const auto settings = asset.data.metadata.at("settings").get<NavigationSettings>();
        if (settings.plane == NavigationPlane::PlanarXY) {
            if (std::hypot(actual[0] - ground[0], actual[1] - ground[1]) > settings.radius + .05)
                throw NavigationFailure(NavStatus::StartOutside,
                                        "2D character is outside the navigation tolerance");
            return actual;
        }
        if (std::hypot(actual[0] - ground[0], actual[2] - ground[2]) > settings.radius + .05 ||
            std::abs(actual[1] - ground[1]) > 2 * settings.cell_height + .1)
            throw NavigationFailure(NavStatus::StartOutside,
                                    "Character is outside the navigation ground tolerance");
        // Projection admits the route surface, but must not hide a physical
        // centre's horizontal offset at an eroded boundary.
        return {actual[0], ground[1], actual[2]};
    }
    Impl(WorldContext& c, std::filesystem::path p)
        : context(c), project(std::move(p)), surfaces(c.world().query<const NavigationSurface>()),
          regions(c.world().query<const NavigationRegion2D>()),
          obstacle_entities(c.world().query<const NavigationObstacle>()),
          link_entities(c.world()
                            .query_builder<const NavigationLink>()
                            .cache_kind(flecs::QueryCacheNone)
                            .query_flags(EcsQueryMatchPrefab | EcsQueryMatchDisabled)
                            .build()),
          source_entities(c.world()
                              .query_builder<const PersistentEntityId>()
                              .cache_kind(flecs::QueryCacheNone)
                              .query_flags(EcsQueryMatchPrefab | EcsQueryMatchDisabled)
                              .build()),
          behaviors(c) {}
    void release_tokens(SourceTokens& tokens) noexcept {
        if (auto resources = source_owner.lock())
            for (const auto& [id, token] : tokens)
                try {
                    resources->release(token);
                } catch (...) { /* provider may already be retired */
                }
        tokens.clear();
        if (mesh_tokens.empty() && collision_tokens.empty())
            source_owner.reset();
    }
    void release_meshes() noexcept { release_tokens(mesh_tokens); }
    void release_resources() noexcept {
        release_tokens(mesh_tokens);
        release_tokens(collision_tokens);
    }
    ~Impl() { release_resources(); }
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
    void capture_geometry(GeometryState& state, const NavigationSettings& settings) {
        const auto plane = settings.plane;
        const std::string_view name = settings.profile;
        auto capture_profile = context.services().profile("navigation", "NavSourceCapture", tick);
        const auto previous_legacy = std::move(state.legacy);
        const auto previous_sources = std::move(state.digest);
        state = {};
        state.captured = synchronization;
        state.selection = settings;
        try {
            const auto source = capture_native(context, *scene, source_entities, settings);
            state.collisions = source.collisions;
            std::shared_ptr<RuntimeResourceService> resources;
            if (plane == NavigationPlane::Spatial3D) {
                if (!source.meshes.empty() || !source.collisions.empty()) {
                    if (!context.services().available(Capability::Resources)) {
                        release_resources();
                        state.status = NavStatus::Unavailable;
                        throw std::runtime_error(
                            "Static Mesh/Collision navigation requires runtime CPU Resources");
                    }
                    resources = context.services().resources();
                    if (source_owner.lock() != resources) {
                        release_resources();
                        source_owner = resources;
                    }
                }
                for (auto it = mesh_tokens.begin(); it != mesh_tokens.end();) {
                    if (!source.meshes.contains(it->first)) {
                        if (auto owner = source_owner.lock())
                            owner->release(it->second);
                        it = mesh_tokens.erase(it);
                    } else
                        ++it;
                }
                for (auto id : source.meshes)
                    if (!mesh_tokens.contains(id))
                        mesh_tokens.emplace(id, resources->request(RuntimeResourceKind::Mesh, id));
                for (auto id : source.collisions)
                    if (!collision_tokens.contains(id))
                        collision_tokens.emplace(
                            id, resources->request(RuntimeResourceKind::Collision, id));
            }
            bool ready = true;
            std::uint64_t retained_bytes = 0;
            const auto inputs = mesh_inputs(source, [&](AssetId id) {
                const auto token = mesh_tokens.at(id);
                const auto status = resources->inspect(token);
                if (status.requested_revision.empty())
                    throw std::runtime_error(status.diagnostic);
                const auto lease = resources->mesh(token);
                if (status.state == "failed")
                    throw std::runtime_error("Navigation Mesh admission failed: " +
                                             status.diagnostic);
                ready &= status.state == "ready" && lease &&
                         status.retained_revision == status.requested_revision;
                if (lease)
                    retained_bytes += lease->resident_bytes();
                return status.requested_revision;
            });
            const auto collisions = collision_inputs(source, [&](AssetId id) {
                const auto status = resources->inspect(collision_tokens.at(id));
                if (status.requested_revision.empty())
                    throw std::runtime_error(status.diagnostic);
                if (status.state == "failed")
                    throw std::runtime_error("Navigation Collision admission failed: " +
                                             status.diagnostic);
                ready &= status.state == "ready" &&
                         status.retained_revision == status.requested_revision;
                return status.requested_revision;
            });
            if (retained_bytes > mesh_bytes) {
                state.status = NavStatus::Limit;
                throw std::runtime_error("Navigation Mesh inputs exceed 64 MiB retained CPU data");
            }
            Json source_inputs = inputs;
            if (plane == NavigationPlane::PlanarXY && !source.maps.empty()) {
                std::map<AssetId, std::string> revisions;
                for (const auto& surface : source.surfaces) {
                    if (!surface.collider || !surface.collider->map)
                        continue;
                    const EntityRef ref{source.scene, surface.entity};
                    const auto status = detail::inspect_runtime_tile_map(context, ref);
                    const auto map = detail::runtime_tile_map_snapshot(context, ref);
                    if (!status.ready || !status.current || !map ||
                        status.requested != surface.collider->map ||
                        status.adopted != surface.collider->map ||
                        map->cells.asset() != surface.collider->map ||
                        status.adopted_revision != status.requested_revision) {
                        state.loading = true;
                        state.status = NavStatus::Unavailable;
                        throw std::runtime_error(
                            status.diagnostic.empty()
                                ? "Waiting for current navigation Tile Map collision inputs"
                                : status.diagnostic);
                    }
                    if (map->edited)
                        throw std::runtime_error(
                            "Navigation source Tile Map has runtime edits; reset the instance "
                            "before following its baked mesh");
                    const auto revision = map->asset.identity().revision;
                    const auto [entry, added] = revisions.emplace(surface.collider->map, revision);
                    if (!added && entry->second != revision)
                        throw std::runtime_error(
                            "Navigation Tile Map instances have different revisions");
                }
                source_inputs = tile_inputs(source, [&](AssetId id) { return revisions.at(id); });
            }
            state.digest = source_digest(source, source_inputs, collisions);
            if (plane == NavigationPlane::Spatial3D && source.meshes.empty() &&
                !settings.collect_static_colliders)
                state.legacy = !previous_legacy.empty() && previous_sources == state.digest
                                   ? previous_legacy
                                   : navigation_detail::geometry(source).digest;
            if (!ready) {
                state.loading = true;
                state.status = NavStatus::Unavailable;
                throw std::runtime_error(
                    "Waiting for selected navigation Mesh/Collision resources");
            }
        } catch (const std::exception& e) {
            state.legacy.clear();
            state.error = e.what();
        }
        // Keep asynchronous subscriptions until adoption; releasing them restarts loading.
        if (!state.loading && !state.error.empty() && name.empty() &&
            plane == NavigationPlane::Spatial3D)
            release_meshes();
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
            cache.emplace(ref.id, Cached{std::move(loaded),
                                         found.record->metadata.at("sha256"),
                                         std::move(query),
                                         {}});
        }
        auto& value = cache.at(ref.id);
        if (value.data.metadata.at("source_scene").get<AssetId>() != scene->asset_id())
            throw NavigationFailure(NavStatus::Invalid,
                                    "NavMesh was built for another scene asset");
        const auto& source = profile_for(value);
        const auto& digest = value.data.metadata.at("version") == 1 ? source.legacy : source.digest;
        if (!digest.empty() &&
            digest != value.data.metadata.at("geometry_sha256").get<std::string>())
            throw NavigationFailure(
                NavStatus::Stale, "Navigation geometry changed; stop Play and rebuild navigation");
        if (!source.error.empty())
            throw NavigationFailure(source.status, source.error);
        if (digest.empty())
            throw NavigationFailure(NavStatus::Stale, "Navigation geometry provenance unavailable");
        if (!obstacle_error.empty())
            throw NavigationFailure(obstacle_status, obstacle_error);
        if (obstacle_sources.empty() && !value.data.mesh->dynamic_obstacles())
            return value; // Preserve the static/default hot path.
        const auto settings = value.data.metadata.at("settings").get<NavigationSettings>();
        std::vector<CylinderObstacle> shapes;
        shapes.reserve(obstacle_sources.size());
        for (const auto& source : obstacle_sources)
            shapes.push_back(
                obstacle_cylinder(source.id, source.component, source.transform, settings));
        if (!value.obstacle_snapshot || *value.obstacle_snapshot != shapes) {
            const bool failed_same =
                value.failed_obstacle_snapshot && *value.failed_obstacle_snapshot == shapes;
            if (failed_same &&
                (context.world().is_readonly() || value.obstacle_attempt == synchronization))
                throw NavigationFailure(NavStatus::Invalid, value.obstacle_update_error);
            if (settings.dynamic_obstacles && context.world().is_readonly())
                throw NavigationFailure(
                    NavStatus::Unavailable,
                    "Navigation obstacle updates require a writable owner boundary");
            auto profile = context.services().profile("navigation", "NavObstacleUpdate", tick);
            value.obstacle_attempt = synchronization;
            try {
                value.query->obstacles(shapes);
                value.obstacle_snapshot = std::move(shapes);
                value.failed_obstacle_snapshot.reset();
                value.obstacle_update_error.clear();
            } catch (const std::exception& error) {
                value.obstacle_snapshot.reset();
                value.failed_obstacle_snapshot = std::move(shapes);
                value.obstacle_update_error = error.what();
                throw;
            }
        }
        return value;
    }
    void capture_obstacles() {
        ApplicationStateActivation conditions(context);
        obstacle_sources.clear();
        obstacle_error.clear();
        obstacle_status = NavStatus::Invalid;
        try {
            context.evaluate_world_transforms();
            std::vector<std::pair<flecs::entity, NavigationObstacle>> candidates;
            obstacle_entities.each([&](flecs::entity e, const NavigationObstacle& obstacle) {
                candidates.emplace_back(e, obstacle);
            });
            for (const auto& [e, obstacle] : candidates) {
                if (!obstacle.enabled || !e.owns<PersistentEntityId>() || e.has(flecs::Prefab))
                    continue;
                if (!conditions.active(e.id()))
                    continue;
                const auto id = e.get<PersistentEntityId>().value;
                if (scene->entity(id.str()) != e)
                    continue;
                if (obstacle_sources.size() >= max_obstacles) {
                    obstacle_status = NavStatus::Limit;
                    throw std::runtime_error("Navigation exceeds eight enabled Scene obstacles");
                }
                detail::validate_reflected_value(e, obstacle);
                if (!e.has<LocalTranslation>() || !e.has<WorldTransform>() ||
                    !e.get<WorldTransform>().resolved)
                    throw std::runtime_error(
                        "Navigation obstacle requires a resolved spatial transform");
                if ((e.has<NavigationSurface>() && e.get<NavigationSurface>().enabled) ||
                    (e.has<NavigationRegion2D>() && e.get<NavigationRegion2D>().enabled) ||
                    (e.has<NavigationAgent>() && e.get<NavigationAgent>().enabled))
                    throw std::runtime_error(
                        "Navigation obstacle cannot also be an enabled baked source or agent");
                obstacle_sources.push_back({id, obstacle, e.get<WorldTransform>().affine});
            }
            std::sort(obstacle_sources.begin(), obstacle_sources.end(),
                      [](const auto& a, const auto& b) { return a.id < b.id; });
        } catch (const std::exception& error) {
            obstacle_sources.clear();
            obstacle_error = error.what();
        }
    }
    void valid_agent(flecs::entity e, const NavigationAgent& a,
                     const std::map<std::uint64_t, TransformNode>& nodes,
                     ApplicationStateActivation& conditions) {
        try {
            detail::validate_reflected_value(e, a);
        } catch (const std::exception& error) {
            throw NavigationFailure(NavStatus::Invalid, error.what());
        }
        if (a.movement == 0 &&
            (e.has<PhysicsBody>() || e.has<PhysicsBody2D>() ||
             (e.has<CharacterController>() && e.get<CharacterController>().enabled) ||
             (e.has<CharacterController2D>() && e.get<CharacterController2D>().enabled)))
            throw NavigationFailure(NavStatus::Invalid,
                                    "NavigationAgent movement requires an entity without a "
                                    "physics body or enabled character controller");
        if (a.movement == 1) {
            if (!e.has<CharacterController>() || !e.get<CharacterController>().enabled ||
                (e.has<PhysicsBody>() && e.get<PhysicsBody>().enabled) ||
                (e.has<PhysicsBody2D>() && e.get<PhysicsBody2D>().enabled) ||
                (e.has<CharacterController2D>() && e.get<CharacterController2D>().enabled) ||
                !e.has<SpatialBinding>() || e.get<SpatialBinding>().mode != SpatialMode::World)
                throw NavigationFailure(NavStatus::Invalid,
                                        "Character navigation requires an enabled 3D Character "
                                        "Controller in World space without another enabled body");
            if (!e.has<WorldTransform>() || !e.get<WorldTransform>().resolved)
                throw NavigationFailure(NavStatus::Invalid,
                                        "Character navigation requires a resolved upright pose");
            const auto up = e.get<WorldTransform>().affine.vector({0, 1, 0});
            if (up[1] <= 0 || std::hypot(up[0], up[2]) > 1e-5 * up[1])
                throw NavigationFailure(NavStatus::Invalid,
                                        "Character navigation requires world +Y up");
        }
        if (a.movement == 2) {
            if (!e.has<CharacterController2D>() || !e.get<CharacterController2D>().enabled ||
                e.get<CharacterController2D>().mode != 1 ||
                (e.has<PhysicsBody>() && e.get<PhysicsBody>().enabled) ||
                (e.has<PhysicsBody2D>() && e.get<PhysicsBody2D>().enabled) ||
                (e.has<CharacterController>() && e.get<CharacterController>().enabled) ||
                !e.has<SpatialBinding>() || e.get<SpatialBinding>().mode != SpatialMode::World)
                throw NavigationFailure(
                    NavStatus::Invalid,
                    "XY character navigation requires an enabled World-space Floating Character "
                    "Controller 2D without another enabled body");
        }
        if ((e.has<NavigationSurface>() && e.get<NavigationSurface>().enabled) ||
            (e.has<NavigationRegion2D>() && e.get<NavigationRegion2D>().enabled))
            throw NavigationFailure(
                NavStatus::Invalid,
                "A moving NavigationAgent cannot be baked navigation source geometry");
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
            if (!conditions.active(current)) {
                current = nodes.at(current).parent;
                continue;
            }
            if ((ancestor.has<PhysicsBody>() && ancestor.get<PhysicsBody>().motion != 0) ||
                (ancestor.has<PhysicsBody2D>() && ancestor.get<PhysicsBody2D>().motion != 0) ||
                (ancestor.has<CharacterController>() &&
                 ancestor.get<CharacterController>().enabled) ||
                (ancestor.has<CharacterController2D>() &&
                 ancestor.get<CharacterController2D>().enabled)) {
                const auto name = ancestor.has<AuthoredName>()
                                      ? detail::json_output_text(
                                            ancestor.get<AuthoredName>().value.substr(0, 256))
                                            .get<std::string>()
                                      : std::string("Unnamed");
                const auto reference = context.reference(current);
                throw NavigationFailure(
                    NavStatus::Invalid,
                    "NavigationAgent cannot follow a moving physics or "
                    "character-controller ancestor: " +
                        name +
                        (reference ? " [" + reference->entity.str() + "]" : " (runtime ancestor)"));
            }
            current = nodes.at(current).parent;
        }
    }
    void valid_avoidance(flecs::entity e, const std::map<std::uint64_t, TransformNode>& nodes,
                         ApplicationStateActivation& conditions) {
        detail::validate_reflected_value(e, e.get<NavigationAvoidance>());
        auto parent = nodes.at(e.id()).parent;
        while (parent) {
            auto ancestor = context.world().entity(parent);
            if (conditions.active(parent) && ancestor.has<NavigationAgent>()) {
                const auto& a = ancestor.get<NavigationAgent>();
                if (a.enabled && a.has_destination && a.speed > 0)
                    throw NavigationFailure(
                        NavStatus::Invalid,
                        "Navigation avoidance requires a stationary spatial parent");
            }
            parent = nodes.at(parent).parent;
        }
    }
};
NavigationRuntime::NavigationRuntime(WorldContext& c, std::filesystem::path p)
    : impl_(std::make_unique<Impl>(c, std::move(p))) {}
NavigationRuntime::~NavigationRuntime() = default;
void NavigationRuntime::shutdown() noexcept { impl_.reset(); }
void NavigationRuntime::bind(Scene* scene) {
    if (impl_) {
        impl_->behaviors.bind(scene);
        impl_->scene = scene;
        impl_->agents.clear();
        impl_->errors.clear();
        impl_->agent_limit = false;
        impl_->obstacle_sources.clear();
        impl_->obstacle_error.clear();
        impl_->release_resources();
        impl_->geometry = {};
        impl_->profile_geometry.clear();
        impl_->linked_profiles.clear();
    }
}
bool NavigationRuntime::prepare_assets() {
    if (!impl_)
        throw std::runtime_error("Navigation owner is unavailable");
    impl_->check();
    return impl_->behaviors.prepare(true);
}
void NavigationRuntime::synchronize() {
    if (!impl_)
        return;
    auto& s = *impl_;
    s.check();
    if (s.context.world().is_readonly())
        throw std::logic_error("Navigation synchronization requires a writable boundary");
    ++s.synchronization;
    s.capture_obstacles();
    ApplicationStateActivation conditions(s.context);
    s.linked_profiles.clear();
    s.link_entities.each([&](flecs::entity e, const NavigationLink& link) {
        if (e.owns<PersistentEntityId>() &&
            s.scene->entity(e.get<PersistentEntityId>().value.str()) == e &&
            conditions.active(e.id()) && link.enabled &&
            link.plane <= unsigned(NavigationPlane::PlanarXY))
            s.linked_profiles.emplace(NavigationPlane(link.plane), link.profile);
    });
    std::set<Impl::ProfileKey> collected_profiles;
    for (const auto& [asset, cached] : s.cache) {
        const auto settings = cached.data.metadata.at("settings").get<NavigationSettings>();
        if (settings.collect_static_colliders)
            collected_profiles.emplace(settings.plane, settings.profile);
    }
    std::erase_if(s.profile_geometry, [&](const auto& entry) {
        return !s.linked_profiles.contains(entry.first) &&
               !collected_profiles.contains(entry.first);
    });
    const auto has_source = [&](auto& query) {
        std::vector<flecs::entity_t> enabled;
        query.each([&](flecs::entity e, const auto& source) {
            if (source.enabled && e.owns<PersistentEntityId>())
                if (s.scene->entity(e.get<PersistentEntityId>().value.str()) == e)
                    enabled.push_back(e.id());
        });
        return std::any_of(enabled.begin(), enabled.end(),
                           [&](auto id) { return conditions.active(id); });
    };
    const std::array<bool, 2> present{has_source(s.surfaces), has_source(s.regions)};
    // Preserve the existing absent-geometry fast path and the shared floor-only
    // capture. Selected link/collection profiles use another bounded derived view.
    for (const auto plane : {NavigationPlane::Spatial3D, NavigationPlane::PlanarXY}) {
        auto& state = s.geometry.at(unsigned(plane));
        if (present[unsigned(plane)]) {
            NavigationSettings settings;
            settings.plane = plane;
            settings.profile.clear();
            s.capture_geometry(state, settings);
        } else {
            state = {};
            state.captured = s.synchronization;
            state.error = plane == NavigationPlane::Spatial3D
                              ? "No enabled NavigationSurface geometry"
                              : "No enabled NavigationRegion2D geometry";
            if (plane == NavigationPlane::Spatial3D)
                s.release_meshes();
        }
    }
    // Read-only C++ controllers may request queries without NavigationAgent.
    // Their cache entries still adopt complete snapshots at the next writable
    // boundary. Failure belongs to that asset/request, not another profile.
    for (const auto& entry : s.cache) {
        try {
            (void)s.get({entry.first});
        } catch (const std::exception&) {
            // get() retains the exact source/update error for the next consumer.
        }
    }
    // Subscriptions pin current copied profile inputs only. In particular, the
    // floor-only capture must not release/re-request a collector's shape each tick.
    const auto resources = s.source_owner.lock();
    std::erase_if(s.collision_tokens, [&](const auto& token) {
        const bool selected = std::any_of(
            s.profile_geometry.begin(), s.profile_geometry.end(), [&](const auto& profile) {
                return profile.second.captured == s.synchronization &&
                       profile.second.collisions.contains(token.first);
            });
        if (!selected && resources)
            resources->release(token.second);
        return !selected;
    });
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
    if (s.awaiting_physics())
        throw std::runtime_error(
            "Complete character physics adoption before another navigation tick");
    ApplicationStateActivation conditions(s.context);
    s.tick = tick;
    synchronize();
    if (!std::isfinite(dt) || dt <= 0 || dt > 1)
        throw std::runtime_error("Navigation requires positive bounded fixed delta");
    auto profile = s.context.services().profile("navigation", "NavAgentUpdate", tick);
    s.context.evaluate_world_transforms();
    auto nodes = s.context.transform_nodes();
    try {
        auto behavior_profile =
            s.context.services().profile("navigation", "NavBehaviorUpdate", tick);
        s.behaviors.begin_tick(dt, tick,
                               [&](auto asset) { return s.get(asset).data.mesh->space(); });
    } catch (const std::exception& error) {
        s.diagnostic(0, {}, error.what());
        s.agents.clear();
        s.agent_limit = true;
        return;
    }
    std::vector<flecs::entity_t> entities;
    s.context.world().each([&](flecs::entity e, const NavigationAgent&) {
        if (e.owns<PersistentEntityId>())
            entities.push_back(e.id());
    });
    std::erase_if(entities, [&](auto id) { return !conditions.active(id); });
    std::set<flecs::entity_t> alive(entities.begin(), entities.end());
    std::erase_if(s.agents, [&](const auto& p) { return !alive.contains(p.first); });
    std::erase_if(s.errors, [&](const auto& p) { return p.first && !alive.contains(p.first); });
    if (entities.size() > 64) {
        s.diagnostic(0, {}, "Navigation exceeds 64 authored agents; no agents moved");
        s.agents.clear();
        s.agent_limit = true;
        return;
    }
    if (s.agent_limit)
        s.errors.erase(0); // Retire the episode so a later limit can emit a fresh warning.
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
    struct Participant {
        AssetId asset;
        MeshSpace space;
        std::uint32_t shared_group;
        Query::Neighbour neighbour;
        float height;
    };
    std::map<flecs::entity_t, Participant> participants;
    for (auto id : entities) {
        auto e = s.context.world().entity(id);
        const auto& a = e.get<NavigationAgent>();
        if (!a.enabled || !e.has<NavigationAvoidance>() || !e.get<NavigationAvoidance>().enabled)
            continue;
        try {
            s.valid_agent(e, a, nodes, conditions);
            s.valid_avoidance(e, nodes, conditions);
            if (const auto previous = s.agents.find(id);
                previous != s.agents.end() && crossing(previous->second))
                continue; // Linear traversal is not a ground-plane avoidance participant.
            auto& asset = s.get(a.navmesh);
            const auto position = e.get<WorldTransform>().affine.point({0, 0, 0});
            if (asset.query->project(position).status != NavStatus::Success)
                continue;
            Double3 velocity{};
            if (const auto previous = s.agents.find(id);
                a.has_destination && a.speed > 0 && previous != s.agents.end() &&
                previous->second.observation.sampled &&
                distance(previous->second.last, position) < .001)
                velocity = previous->second.observation.velocity;
            const auto& settings = asset.data.metadata.at("settings");
            participants.emplace(
                id, Participant{a.navmesh.id,
                                asset.data.mesh->space(),
                                e.get<NavigationAvoidance>().shared_group,
                                {position, velocity, settings.at("radius").get<float>()},
                                settings.at("height").get<float>()});
        } catch (const std::exception&) {
            // Invalid or unavailable agents are not predictive obstacles. Active
            // movement below supplies the existing diagnostic/observation policy.
        }
    }
    for (auto id : entities) {
        auto e = s.context.world().entity(id);
        auto a = e.get<NavigationAgent>();
        if (!a.enabled || !a.has_destination) {
            const auto previous = s.agents.find(id);
            if (previous != s.agents.end() && !previous->second.physical_jump &&
                crossing(previous->second) && previous->second.config.navmesh == a.navmesh) {
                previous->second.config = a;
                previous->second.replan_after_link = true;
            } else {
                s.agents.erase(id);
            }
            s.errors.erase(id);
            continue;
        }
        std::optional<EntityId> retained_link;
        std::optional<std::pair<std::size_t, std::size_t>> retained_progress;
        if (const auto previous = s.agents.find(id); previous != s.agents.end())
            if (const auto* link = crossing(previous->second))
                retained_link = link->source;
        try {
            s.context.evaluate_world_transforms();
            s.valid_agent(e, a, nodes, conditions);
            const bool avoiding =
                e.has<NavigationAvoidance>() && e.get<NavigationAvoidance>().enabled;
            if (avoiding)
                s.valid_avoidance(e, nodes, conditions);
            auto& asset = s.get(a.navmesh);
            auto position = e.get<WorldTransform>().affine.point({0, 0, 0});
            if (a.movement == 1 && asset.data.mesh->space() != NavigationPlane::Spatial3D)
                throw NavigationFailure(NavStatus::Invalid,
                                        "Character navigation supports the 3D ground plane");
            if (a.movement == 2 && asset.data.mesh->space() != NavigationPlane::PlanarXY)
                throw NavigationFailure(NavStatus::Invalid,
                                        "2D character navigation requires an XY NavMesh");
            Double3 destination{a.destination_x, a.destination_y, a.destination_z};
            auto& state = s.agents[id];
            if (crossing(state) && state.config.navmesh == a.navmesh &&
                state.config.movement == a.movement &&
                distance(state.last, position) <= retained_pose_tolerance(state)) {
                retained_progress = {*state.crossing, state.next};
                state.replan_after_link |= state.config.has_destination != a.has_destination ||
                                           state.config.destination_x != a.destination_x ||
                                           state.config.destination_y != a.destination_y ||
                                           state.config.destination_z != a.destination_z;
                state.config = a;
                const bool changed = state.query_generation != asset.query->generation();
                if (changed || crossing(state)->arc_height != 0) {
                    const auto* link = crossing(state);
                    if (!asset.query->link_available(link->source, state.path.at(link->entry),
                                                     state.path.at(link->exit), link->arc_height))
                        throw NavigationFailure(NavStatus::NoPath,
                                                "Navigation link exit is blocked; traversal held");
                    state.query_generation = asset.query->generation();
                    state.replan_after_link |= changed;
                }
            } else if (avoiding || state.config != a || state.path.empty() ||
                       state.replan_after_link ||
                       state.query_generation != asset.query->generation() ||
                       distance(state.last, position) > retained_pose_tolerance(state)) {
                retained_link.reset();
                auto route = asset.query->route(position, destination);
                const auto previous = state.observation;
                state = {};
                state.config = a;
                state.path = std::move(route.path.points);
                state.links = std::move(route.links);
                state.next = 1;
                state.last = position;
                state.query_generation = asset.query->generation();
                if (avoiding)
                    state.observation = previous;
                if (route.path.status != NavStatus::Success)
                    throw NavigationFailure(route.path.status, route.path.diagnostic);
            }
            if (state.path.empty())
                continue;
            if ((a.movement == 2 && !state.links.empty()) ||
                (a.movement == 1 && std::ranges::any_of(state.links, [](const auto& link) {
                     return link.arc_height <= 0;
                 })))
                throw NavigationFailure(NavStatus::Invalid,
                                        "Character links require 3D movement and positive lift");
            const auto character_position = position;
            if (a.movement == 1 && !state.links.empty()) {
                const auto physics = s.context.services().available(Capability::Physics)
                                         ? s.context.services().physics()
                                         : nullptr;
                const auto ref = s.context.reference(id);
                if (!physics || !ref)
                    throw NavigationFailure(NavStatus::Unavailable,
                                            "Character jump requires the physics service");
                const auto gravity = physics->gravity();
                const auto acceleration = -gravity[1] * e.get<CharacterController>().gravity_factor;
                if (std::abs(gravity[0]) > 1e-8 || std::abs(gravity[2]) > 1e-8 ||
                    !std::isfinite(acceleration) || acceleration <= 0)
                    throw NavigationFailure(NavStatus::Invalid,
                                            "Character jump requires downward vertical gravity");
                std::optional<std::size_t> jump = state.crossing;
                if (!jump && a.speed > 0)
                    for (std::size_t i = 0; i != state.links.size(); ++i) {
                        const auto& link = state.links[i];
                        const auto entry = state.path.at(link.entry);
                        if ((state.next == link.entry || state.next == link.exit) &&
                            std::hypot(position[0] - entry[0], position[2] - entry[2]) < .001) {
                            if (route_distance(Impl::character_route_origin(asset, position),
                                               state) > a.stopping_distance + arrival_tolerance)
                                jump = i;
                            break;
                        }
                    }
                if (jump) {
                    const auto& link = state.links.at(*jump);
                    const auto entry = state.path.at(link.entry), exit = state.path.at(link.exit);
                    const auto duration = std::sqrt(8 * double(link.arc_height) / acceleration);
                    const auto horizontal = std::hypot(exit[0] - entry[0], exit[2] - entry[2]);
                    const auto flight_speed = horizontal / duration;
                    const bool takeoff = !state.physical_jump;
                    float jump_speed = 0;
                    if (takeoff) {
                        const auto character = physics->character(*ref);
                        const auto support = character.supporting_entity
                                                 ? physics->body_state(*character.supporting_entity)
                                                 : std::nullopt;
                        const auto settings =
                            asset.data.metadata.at("settings").get<NavigationSettings>();
                        const auto rise = exit[1] - position[1];
                        const auto speed = (rise + 4 * double(link.arc_height)) / duration +
                                           .5 * acceleration * dt;
                        if (character.ground != CharacterGround::OnGround || !support ||
                            support->motion != 0 ||
                            std::hypot(character.ground_velocity[0], character.ground_velocity[1],
                                       character.ground_velocity[2]) > 1e-5 ||
                            std::abs(position[1] - entry[1]) > 2 * settings.cell_height + .1)
                            throw NavigationFailure(
                                NavStatus::Invalid,
                                "Character jump requires static walkable takeoff support");
                        const std::array exclusions{*ref};
                        const double tolerance = 2 * settings.cell_height + .1;
                        const auto landing = physics->raycast_filtered(
                            {exit[0], exit[1] + tolerance, exit[2]}, {0, -2 * tolerance, 0},
                            {e.get<CharacterController>().mask, false, exclusions});
                        const auto landing_body =
                            landing ? physics->body_state(landing->entity) : std::nullopt;
                        if (!landing_body || landing_body->motion != 0)
                            throw NavigationFailure(
                                NavStatus::Invalid,
                                "Character jump requires static landing support");
                        if (!std::isfinite(speed) || speed <= 0 || speed > 1000000 ||
                            std::abs(rise) >= 4 * double(link.arc_height) || duration < 4 * dt ||
                            flight_speed > a.speed + 1e-6)
                            throw NavigationFailure(
                                NavStatus::Invalid,
                                "Character jump exceeds speed, lift or fixed-step limits");
                        jump_speed = float(speed);
                    }
                    const auto gap = std::hypot(exit[0] - position[0], exit[2] - position[2]);
                    const auto speed = std::min({flight_speed, double(a.speed), gap / dt});
                    const Double3 velocity = gap > 1e-8
                                                 ? Double3{(exit[0] - position[0]) * speed / gap, 0,
                                                           (exit[2] - position[2]) * speed / gap}
                                                 : Double3{};
                    if (takeoff)
                        physics->jump_character_once(*ref, velocity, jump_speed);
                    else
                        physics->move_character_once(*ref, velocity);
                    state.crossing = jump;
                    state.physical_jump = true;
                    state.next = link.exit;
                    state.character_frame = AgentState::CharacterFrame{character_position, takeoff};
                    state.observation = {};
                    state.observation.phase = NavigationAgentPhase::Pending;
                    continue;
                }
            }
            if (a.movement != 0) {
                position = Impl::character_route_origin(asset, position);
                state.next = std::min(state.next, state.path.size() - 1);
            }
            const auto original_next = state.next;
            double remaining = double(a.speed) * dt;
            const auto original = position;
            std::optional<NavigationLinkCollision> collision;
            if (e.has<NavigationLinkCollision>()) {
                const auto value = e.get<NavigationLinkCollision>();
                validate_link_collision(value);
                if (value.enabled && value.mask != 0)
                    collision = value;
            }
            unsigned collision_queries = 0;
            std::shared_ptr<PhysicsService> collision_world;
            std::optional<EntityRef> blocker;
            const auto clear_segment = [&](Double3 from, Double3 to, double padding) {
                if (!collision)
                    return true;
                if (collision_queries == link_collision_cast_limit)
                    throw NavigationFailure(NavStatus::Invalid,
                                            "Navigation link collision query budget exceeded; "
                                            "reduce speed or fixed delta");
                ++collision_queries;
                if (!collision_world && s.context.services().available(Capability::Physics))
                    collision_world = s.context.services().physics();
                if (!collision_world)
                    throw NavigationFailure(
                        NavStatus::Unavailable,
                        "Navigation link collision requires the runtime physics service");
                const auto radius = std::nextafter(float(double(collision->radius) + padding),
                                                   std::numeric_limits<float>::infinity());
                const PhysicsQueryFilter filter{collision->mask, collision->include_sensors};
                if (asset.data.mesh->space() == NavigationPlane::PlanarXY) {
                    PhysicsSweep2D query;
                    query.origin = {from[0], from[1]};
                    query.displacement = {to[0] - from[0], to[1] - from[1]};
                    query.dimensions = {radius, 0};
                    const auto hit = collision_world->shape_cast_2d(query, filter);
                    if (hit)
                        blocker = hit->entity;
                } else {
                    PhysicsSweep query;
                    query.shape = PhysicsSweep::Shape::Capsule;
                    query.origin = {from[0],
                                    from[1] + double(collision->radius) +
                                        double(collision->straight_height) * .5 +
                                        collision->clearance,
                                    from[2]};
                    query.dimensions = {radius, collision->straight_height, 0};
                    for (unsigned axis = 0; axis != 3; ++axis)
                        query.displacement[axis] = to[axis] - from[axis];
                    const auto hit = collision_world->shape_cast(query, filter);
                    if (hit)
                        blocker = hit->entity;
                }
                return !blocker;
            };
            if (a.movement == 0 && remaining > 0 &&
                route_distance(position, state) > a.stopping_distance + arrival_tolerance)
                begin_crossing(state, position, remaining);
            if (avoiding && !crossing(state) && remaining > 0 &&
                route_distance(position, state) > a.stopping_distance + arrival_tolerance) {
                auto avoidance_profile =
                    s.context.services().profile("navigation", "NavAvoidance", tick);
                while (state.next < state.path.size() &&
                       distance(position, state.path[state.next]) < .001 &&
                       !arc_entry(state, state.next))
                    ++state.next;
                if (state.next < state.path.size() &&
                    (a.movement != 0 || !begin_crossing(state, position, remaining))) {
                    const auto waypoint = state.path[state.next];
                    const auto length = distance(position, waypoint);
                    const auto speed =
                        std::min(double(a.speed), std::min(length, route_distance(position, state) -
                                                                       a.stopping_distance) /
                                                      dt);
                    Double3 desired{};
                    for (unsigned j = 0; j < 3; ++j)
                        desired[j] = (waypoint[j] - position[j]) * speed / length;
                    std::vector<Query::Neighbour> neighbours;
                    const auto settings =
                        asset.data.metadata.at("settings").get<NavigationSettings>();
                    for (const auto& [other, participant] : participants)
                        if (other != id && participant.space == asset.data.mesh->space() &&
                            (participant.asset == a.navmesh.id ||
                             (e.get<NavigationAvoidance>().shared_group != 0 &&
                              participant.shared_group ==
                                  e.get<NavigationAvoidance>().shared_group)) &&
                            (settings.plane == NavigationPlane::PlanarXY ||
                             std::abs(participant.neighbour.position[1] - position[1]) <
                                 std::max(settings.height, participant.height)))
                            neighbours.push_back(participant.neighbour);
                    const auto previous = participants.contains(id)
                                              ? participants.at(id).neighbour.velocity
                                              : Double3{};
                    auto steering = asset.query->avoid_velocity(
                        position, desired, previous, float(speed), settings.radius,
                        e.get<NavigationAvoidance>().horizon, neighbours);
                    if (steering.status != NavStatus::Success)
                        throw NavigationFailure(steering.status,
                                                "Navigation avoidance: " + steering.diagnostic);
                    NavResult moved;
                    double fraction = 1;
                    const auto budget = speed * dt;
                    bool bounded = false;
                    for (unsigned attempt = 0; attempt != 8; ++attempt) {
                        Double3 target = position;
                        for (unsigned j = 0; j < 3; ++j)
                            target[j] += steering.points.front()[j] * dt * fraction;
                        moved = asset.query->move(position, target);
                        if (moved.status != NavStatus::Success)
                            throw NavigationFailure(moved.status, moved.diagnostic);
                        const auto length = distance(position, moved.points.front());
                        if (length <= budget + .000001 || a.movement != 0) {
                            bounded = true;
                            break;
                        }
                        // The sampler works in XZ; surface height must also fit the
                        // existing world-distance speed budget on slopes/steps.
                        fraction *= .999 * budget / length;
                    }
                    if (!bounded)
                        throw NavigationFailure(
                            NavStatus::Invalid,
                            "Avoidance start/surface height cannot fit the fixed-tick speed "
                            "budget; align the agent with navigation");
                    position = moved.points.front();
                    if (distance(position, waypoint) < .001 &&
                        (!arc_entry(state, state.next) || position == waypoint))
                        ++state.next;
                }
            }
            if (avoiding && !crossing(state))
                remaining = 0;
            while (remaining > 0 && state.next < state.path.size()) {
                const auto distance_left = route_distance(position, state);
                if (!crossing(state) && distance_left <= a.stopping_distance + arrival_tolerance)
                    break;
                if (a.movement == 0)
                    begin_crossing(state, position, remaining);
                auto waypoint = state.path[state.next];
                if (const auto* link = crossing(state); link && link->arc_height != 0) {
                    const NavigationLinkCurve curve(state.path[link->entry], waypoint,
                                                    link->arc_height);
                    const auto projected = curve.parameter(position);
                    if (projected < -1e-9 || projected > 1 + 1e-9)
                        throw NavigationFailure(NavStatus::Invalid,
                                                "Navigation arc pose is outside its span");
                    const auto from = std::clamp(projected, 0., 1.);
                    if (distance(position, curve.point(from)) > 1e-6)
                        throw NavigationFailure(NavStatus::Invalid,
                                                "Navigation arc pose is off its curve");
                    const auto advance = std::min(remaining, curve.length(from));
                    const auto to = curve.advance(from, advance);
                    if (to == from && from != 1)
                        break;
                    if (collision) {
                        auto progress = from;
                        while (progress < to) {
                            const auto end =
                                curve.sweep_end(progress, to, double(collision->clearance) * .5);
                            if (!clear_segment(curve.point(progress), curve.point(end),
                                               curve.chord_deviation(progress, end)))
                                break;
                            progress = end;
                        }
                        position = curve.point(progress);
                        remaining -= curve.length(from, progress);
                        if (blocker) {
                            remaining = 0;
                            break;
                        }
                    } else {
                        position = curve.point(to);
                        remaining -= advance;
                    }
                    if (to == 1) {
                        ++state.next;
                        state.crossing.reset();
                        if (state.replan_after_link || avoiding)
                            break;
                    }
                    continue;
                }
                double d = distance(position, waypoint);
                if (d < 1e-6 && !arc_entry(state, state.next)) {
                    ++state.next;
                    if (state.crossing) {
                        state.crossing.reset();
                        if (state.replan_after_link || avoiding)
                            break;
                    }
                    continue;
                }
                const auto advance =
                    crossing(state) ? std::min(remaining, d)
                                    : std::min({remaining, d,
                                                std::max(0., distance_left - a.stopping_distance)});
                if (advance < 1e-8)
                    break;
                Double3 desired;
                for (unsigned j = 0; j < 3; ++j)
                    desired[j] = position[j] + (waypoint[j] - position[j]) * (advance / d);
                auto next = desired;
                if (!crossing(state)) {
                    auto moved = asset.query->move(position, desired);
                    if (moved.status != NavStatus::Success)
                        throw NavigationFailure(moved.status, moved.diagnostic);
                    next = moved.points.front();
                }
                if (crossing(state) && !clear_segment(position, next, 0)) {
                    remaining = 0;
                    break;
                }
                if (distance(next, position) < 1e-8)
                    break;
                position = next;
                remaining -= advance;
                if (distance(position, waypoint) < .001 &&
                    (!arc_entry(state, state.next) || position == waypoint)) {
                    ++state.next;
                    // A physical step follows one velocity segment. Do not turn past an
                    // unobserved corner; adoption advances actual progress after this step.
                    if (a.movement != 0)
                        break;
                    if (state.crossing) {
                        state.crossing.reset();
                        if (state.replan_after_link || avoiding)
                            break;
                    }
                }
            }
            if (a.movement != 0) {
                std::shared_ptr<PhysicsService> physics;
                if (s.context.services().available(Capability::Physics))
                    physics = s.context.services().physics();
                if (!physics)
                    throw NavigationFailure(NavStatus::Unavailable,
                                            "Character navigation requires the physics service");
                Double3 velocity{(position[0] - original[0]) / dt,
                                 a.movement == 2 ? (position[1] - original[1]) / dt : 0,
                                 a.movement == 1 ? (position[2] - original[2]) / dt : 0};
                const auto length = std::hypot(velocity[0], velocity[1], velocity[2]);
                if (length > a.speed && length > 0) {
                    for (auto& component : velocity)
                        component *= a.speed / length;
                }
                const auto reference = s.context.reference(id);
                if (!reference)
                    throw NavigationFailure(NavStatus::Invalid,
                                            "Character navigation has no stable entity reference");
                if (a.movement == 2)
                    physics->move_character_2d_once(*reference, {velocity[0], velocity[1]});
                else
                    physics->move_character_once(*reference, velocity);
                state.next = original_next;
                state.character_frame = AgentState::CharacterFrame{character_position};
                state.observation = {};
                state.observation.phase = NavigationAgentPhase::Pending;
                continue; // Only physics adoption can publish actual motion or route progress.
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
            out.phase = blocker ? NavigationAgentPhase::Blocked
                        : !crossing(state) && !state.replan_after_link &&
                                out.remaining_distance <= a.stopping_distance + arrival_tolerance
                            ? NavigationAgentPhase::Arrived
                        : a.speed == 0                        ? NavigationAgentPhase::Paused
                        : distance(position, original) > 1e-8 ? NavigationAgentPhase::Moving
                                                              : NavigationAgentPhase::Blocked;
            if (blocker)
                s.diagnostic(id, a.navmesh.id,
                             "Navigation link crossing blocked by collision with " +
                                 blocker->scene.str() + "/" + blocker->entity.str());
            else
                s.errors.erase(id);
        } catch (const std::exception& error) {
            auto& state = s.agents[id];
            // Movement publishes once per tick. If a later segment fails, keep
            // the original link continuation alongside the unchanged native pose.
            if (retained_progress) {
                state.crossing = retained_progress->first;
                state.next = retained_progress->second;
            }
            const bool held =
                retained_link && crossing(state) && crossing(state)->source == *retained_link &&
                state.config.navmesh == a.navmesh && e.has<WorldTransform>() &&
                e.get<WorldTransform>().resolved &&
                distance(state.last, e.get<WorldTransform>().affine.point({0, 0, 0})) <=
                    retained_pose_tolerance(state);
            if (!held)
                state = {};
            else
                state.observation = {};
            state.config = a;
            const auto* failure = dynamic_cast<const NavigationFailure*>(&error);
            const bool pending = failure && failure->status == NavStatus::Unavailable &&
                                 s.cache.contains(a.navmesh.id) &&
                                 s.profile_for(s.cache.at(a.navmesh.id)).loading;
            state.observation.phase = pending ? NavigationAgentPhase::Pending
                                      : held && failure && failure->status == NavStatus::NoPath
                                          ? NavigationAgentPhase::Blocked
                                          : NavigationAgentPhase::Failed;
            state.observation.status = failure ? failure->status : NavStatus::Invalid;
            state.observation.tick = pending ? 0 : tick;
            // A failed movement tick has no valid path/pose/velocity sample.
            if (!pending)
                s.diagnostic(id, a.navmesh.id, error.what());
        }
    }
    {
        auto behavior_profile =
            s.context.services().profile("navigation", "NavBehaviorTransitions", tick);
        s.behaviors.finish_tick(dt, [this](auto entity) { return agent_state(entity); });
    }
}
void NavigationRuntime::physics_adopted(float dt, std::uint64_t tick) {
    if (!impl_)
        return;
    auto& s = *impl_;
    s.check();
    if (!s.awaiting_physics())
        return;
    if (tick != s.tick || !std::isfinite(dt) || dt <= 0 || dt > 1)
        throw std::runtime_error("Character navigation feedback requires its matching fixed tick");
    auto profile = s.context.services().profile("navigation", "NavCharacterFeedback", tick);
    std::shared_ptr<PhysicsService> physics;
    if (s.context.services().available(Capability::Physics))
        physics = s.context.services().physics();
    for (auto& [id, state] : s.agents) {
        if (!state.character_frame)
            continue;
        const auto frame = *state.character_frame;
        state.character_frame.reset();
        auto entity = s.context.world().entity(id);
        if (!entity.is_alive() || !entity.has<NavigationAgent>() ||
            entity.get<NavigationAgent>() != state.config)
            continue;
        const auto& config = state.config;
        try {
            const auto reference = s.context.reference(id);
            if (!physics || !reference)
                throw NavigationFailure(NavStatus::Unavailable,
                                        "Character navigation physics observation unavailable");
            Double3 actual{}, velocity{};
            std::optional<CharacterState> spatial_character;
            if (config.movement == 2) {
                const auto character = physics->character_2d(*reference);
                actual = {character.position[0], character.position[1],
                          entity.get<LocalTranslation>().z};
                velocity = {character.velocity[0], character.velocity[1], 0};
                for (auto& point : state.path)
                    point[2] = actual[2]; // Visual depth is independent of physical XY travel.
            } else {
                const auto character = physics->character(*reference);
                spatial_character = character;
                actual = {character.position.x, character.position.y, character.position.z};
                velocity = character.velocity;
            }
            auto& asset = s.get(config.navmesh);
            if (state.physical_jump) {
                const auto* link = crossing(state);
                if (!link || !spatial_character)
                    throw NavigationFailure(NavStatus::Invalid,
                                            "Character jump continuation is unavailable");
                const auto& character = *spatial_character;
                if (frame.jump_requested && !character.jump_accepted)
                    throw NavigationFailure(NavStatus::Invalid,
                                            "Character jump was not accepted at takeoff");
                const auto entry = state.path.at(link->entry), exit = state.path.at(link->exit);
                const auto settings = asset.data.metadata.at("settings").get<NavigationSettings>();
                const bool landed = !frame.jump_requested &&
                                    character.ground == CharacterGround::OnGround &&
                                    character.velocity[1] <= character.ground_velocity[1] + 1e-5;
                if (landed) {
                    const auto support = character.supporting_entity
                                             ? physics->body_state(*character.supporting_entity)
                                             : std::nullopt;
                    if (!support || support->motion != 0 ||
                        std::hypot(actual[0] - exit[0], actual[2] - exit[2]) >
                            settings.radius + .05 ||
                        std::abs(actual[1] - exit[1]) > 2 * settings.cell_height + .1)
                        throw NavigationFailure(NavStatus::NoPath,
                                                "Character jump landed outside its static exit");
                    state.next = link->exit + 1;
                    state.crossing.reset();
                    state.physical_jump = false;
                    state.replan_after_link = true;
                } else {
                    if (actual[1] < std::min(entry[1], exit[1]) - settings.height)
                        throw NavigationFailure(NavStatus::NoPath,
                                                "Character jump missed its landing");
                    auto& out = state.observation;
                    out = {};
                    out.tick = tick;
                    out.sampled = out.has_path = true;
                    out.status = NavStatus::Success;
                    out.position = actual;
                    out.velocity = velocity;
                    out.projected_destination = state.path.back();
                    out.remaining_distance = route_distance(actual, state);
                    out.phase = config.speed == 0 ? NavigationAgentPhase::Paused
                                                  : NavigationAgentPhase::Moving;
                    state.last = actual;
                    s.errors.erase(id);
                    continue;
                }
            }
            const auto ground = Impl::character_route_origin(asset, actual);
            const auto travel =
                config.movement == 2
                    ? std::hypot(actual[0] - frame.original[0], actual[1] - frame.original[1])
                    : std::hypot(actual[0] - frame.original[0], actual[2] - frame.original[2]);
            // XY shallow recovery can correct the pose while all requested
            // motion is clipped. Its copied velocity excludes that correction.
            const bool moving =
                travel > 1e-8 && (config.movement != 2 || std::hypot(velocity[0], velocity[1]) > 0);
            // Mesh projection is route geometry only; the observed pose stays physical.
            while (state.next < state.path.size() &&
                   distance(ground, state.path[state.next]) < .001 &&
                   !(config.movement == 1 && arc_entry(state, state.next)))
                ++state.next;
            auto& out = state.observation;
            out = {};
            out.tick = tick;
            out.sampled = out.has_path = true;
            out.status = NavStatus::Success;
            out.position = actual;
            out.velocity = velocity;
            out.projected_destination = state.path.back();
            out.remaining_distance = route_distance(ground, state);
            out.phase = state.replan_after_link ? NavigationAgentPhase::Pending
                        : out.remaining_distance <= config.stopping_distance + arrival_tolerance
                            ? NavigationAgentPhase::Arrived
                        : config.speed == 0 ? NavigationAgentPhase::Paused
                        : moving            ? NavigationAgentPhase::Moving
                                            : NavigationAgentPhase::Blocked;
            state.last = actual;
            const auto settings = asset.data.metadata.at("settings").get<NavigationSettings>();
            if (travel > config.speed * dt + settings.radius * 2 + .05) {
                state.path.clear();
                out.has_path = false;
                out.phase = NavigationAgentPhase::Pending;
            }
            s.errors.erase(id);
        } catch (const std::exception& error) {
            state.physical_jump = false;
            state.crossing.reset();
            state.path.clear();
            state.observation = {};
            state.observation.tick = tick;
            state.observation.phase = NavigationAgentPhase::Failed;
            const auto* failure = dynamic_cast<const NavigationFailure*>(&error);
            state.observation.status = failure ? failure->status : NavStatus::Invalid;
            s.diagnostic(id, config.navmesh.id, error.what());
        }
    }
    s.behaviors.finish_tick(dt, [this](auto entity) { return agent_state(entity); }, true);
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
    ApplicationStateActivation conditions(s.context);
    if (!config.enabled || !conditions.active(entity.id()))
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
    Json link = nullptr;
    if (const auto state = impl_->agents.find(entity); state != impl_->agents.end())
        if (const auto* current = crossing(state->second)) {
            link = {{"source", current->source},
                    {"entry", state->second.path.at(current->entry)},
                    {"exit", state->second.path.at(current->exit)}};
            if (current->arc_height != 0)
                link["arc_height"] = current->arc_height;
            if (state->second.physical_jump)
                link["physical_jump"] = true;
        }
    Json diagnostic = "";
    if (out->phase == NavigationAgentPhase::Failed) {
        const auto error = impl_->errors.find(impl_->agent_limit ? 0 : entity);
        if (error != impl_->errors.end())
            diagnostic = detail::json_output_text(error->second.substr(0, 2048));
    }
    return {{"path", path},
            {"link", link},
            {"diagnostic", std::move(diagnostic)},
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
Json NavigationRuntime::debug_behavior(flecs::entity_t entity) const {
    return impl_ ? impl_->behaviors.debug(entity) : Json();
}
Json NavigationRuntime::tune(EntityRef ref, const std::string& type, const std::string& property,
                             const Json& value) {
    if (!impl_ || impl_->context.role() != WorldRole::Runtime ||
        !impl_->context.on_owner_thread() || impl_->context.world().is_readonly())
        throw std::runtime_error("Live navigation tuning requires the runtime navigation owner");
    impl_->check();
    if (!RuntimePropertyTune::navigation_property(type, property))
        throw std::runtime_error("Live navigation property is unavailable");
    const auto resolved = impl_->context.resolve(ref);
    if (resolved.state != WorldContext::ResolveState::Available ||
        ref.scene != impl_->scene->asset_id())
        throw std::runtime_error("Live navigation target is unavailable");
    auto e = impl_->context.world().entity(resolved.entity);
    if (impl_->scene->entity(ref.entity.str()) != e)
        throw std::runtime_error("Live navigation target belongs to another Scene instance");
    if (e.has(flecs::Prefab) || !e.has<NavigationAgent>() ||
        (type == "forge.navigation_avoidance" && !e.has<NavigationAvoidance>()) ||
        (type == "forge.navigation_link_collision" && !e.has<NavigationLinkCollision>()) ||
        (type == "forge.navigation_behavior" && !e.has<NavigationBehavior>()))
        throw std::runtime_error(
            "Live navigation tuning requires an existing navigation participant");
    const auto& codec = navigation_descriptor(type);
    auto candidate = codec.read(e, true);
    if (!candidate.contains(property))
        throw std::runtime_error("Live navigation property is unavailable");
    candidate[property] = value;
    detail::validate_components({{type, candidate}});
    const auto decoded = codec.decode(candidate);
    if (type == "forge.navigation_agent") {
        const auto& typed = std::get<NavigationAgent>(decoded);
        detail::validate_reflected_value(e, typed);
        e.set<NavigationAgent>(typed);
    } else if (type == "forge.navigation_behavior") {
        const auto& typed = std::get<NavigationBehavior>(decoded);
        detail::validate_reflected_value(e, typed);
        e.set<NavigationBehavior>(typed);
    } else if (type == "forge.navigation_link_collision") {
        const auto& typed = std::get<NavigationLinkCollision>(decoded);
        detail::validate_reflected_value(e, typed);
        e.set<NavigationLinkCollision>(typed);
    } else {
        const auto& typed = std::get<NavigationAvoidance>(decoded);
        detail::validate_reflected_value(e, typed);
        e.set<NavigationAvoidance>(typed);
    }
    return codec.read(e, true).at(property);
}
Json NavigationRuntime::tune_avoidance(EntityRef ref, const std::string& property,
                                       const Json& value) {
    return tune(ref, "forge.navigation_avoidance", property, value);
}
Json NavigationRuntime::checkpoint() {
    if (impl_ && impl_->awaiting_physics())
        throw std::runtime_error(
            "Navigation recovery requires completed character physics adoption");
    const auto behaviors = impl_ ? impl_->behaviors.checkpoint() : Json::array();
    if (behaviors.is_null())
        return Json();
    Json revisions = Json::array(), agents = Json::array();
    bool has_crossing = false, has_arc = false, has_collision = false, has_character = false,
         has_planar_character = false, has_physical_jump = false;
    if (impl_) {
        impl_->check();
        ApplicationStateActivation conditions(impl_->context);
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
            Json avoidance = nullptr;
            if (e.has<NavigationAvoidance>()) {
                auto value = navigation_descriptor("forge.navigation_avoidance").read(e, true);
                detail::validate_components({{"forge.navigation_avoidance", value}});
                avoidance = {{"config", value}, {"owned", e.owns<NavigationAvoidance>()}};
            }
            Json translation = nullptr;
            const auto movement = e.get<NavigationAgent>().movement;
            has_planar_character |= movement == 2;
            if (movement != 0)
                has_character = true; // Scene/physics recovery owns the character pose.
            else {
                try {
                    impl_->valid_agent(e, e.get<NavigationAgent>(), nodes, conditions);
                    auto t = e.get<LocalTranslation>();
                    translation = {t.x, t.y, t.z};
                } catch (const NavigationFailure&) { /* No navigation transform authority. */
                }
            }
            agents.push_back(
                {{"entity", *ref},
                 {"config", config},
                 {"owned", e.owns<NavigationAgent>()},
                 {"avoidance", avoidance},
                 {"translation", translation},
                 {"translation_owned", !translation.is_null() && e.owns<LocalTranslation>()}});
            if (e.has<NavigationLinkCollision>()) {
                const auto value = link_collision_value(e.get<NavigationLinkCollision>());
                agents.back()["collision"] = {{"config", value},
                                              {"owned", e.owns<NavigationLinkCollision>()}};
                has_collision = true;
            }
            if (const auto state = impl_->agents.find(e.id()); state != impl_->agents.end())
                if (const auto* link = crossing(state->second);
                    link &&
                    (!translation.is_null() || (movement == 1 && state->second.physical_jump)) &&
                    state->second.config.navmesh == e.get<NavigationAgent>().navmesh &&
                    distance(state->second.last, e.get<WorldTransform>().affine.point({0, 0, 0})) <=
                        retained_pose_tolerance(state->second)) {
                    agents.back()["link"] = {{"source", link->source},
                                             {"entry", state->second.path.at(link->entry)},
                                             {"exit", state->second.path.at(link->exit)}};
                    if (link->arc_height != 0) {
                        agents.back()["link"]["arc_height"] = link->arc_height;
                        has_arc = true;
                    }
                    if (state->second.physical_jump) {
                        agents.back()["link"]["physical_jump"] = true;
                        has_physical_jump = true;
                    }
                    has_crossing = true;
                }
        });
    }
    if (has_collision)
        for (auto& entry : agents)
            if (!entry.contains("collision"))
                entry["collision"] = nullptr;
    Json result = {{"version", has_physical_jump      ? 9
                               : has_planar_character ? 8
                               : has_character        ? 7
                               : has_collision        ? 6
                               : has_arc              ? 5
                               : has_crossing         ? 4
                               : behaviors.empty()    ? 2
                                                      : 3},
                   {"assets", revisions},
                   {"agents", agents}};
    if (has_character || has_collision || has_crossing || !behaviors.empty())
        result["behaviors"] = behaviors;
    return result;
}
void NavigationRuntime::restore(const Json& data) {
    if (!impl_ || !data.is_object() || !data.contains("version") ||
        !data.at("version").is_number_integer())
        throw std::runtime_error("Invalid navigation recovery version");
    const auto version = data.at("version").get<std::uint64_t>();
    if (version < 1 || version > 9 || !data.at("assets").is_array() ||
        data.at("assets").size() > 8 || !data.at("agents").is_array() ||
        data.at("agents").size() > 64)
        throw std::runtime_error("Invalid navigation recovery metadata");
    const bool collision_profile =
        version == 6 ||
        (version >= 7 && std::ranges::any_of(data.at("agents"), [](const auto& entry) {
             return entry.contains("collision");
         }));
    impl_->check();
    ApplicationStateActivation conditions(impl_->context);
    struct Pending {
        flecs::entity entity;
        NavigationAgent agent;
        std::optional<NavigationAvoidance> avoidance;
        std::optional<NavigationLinkCollision> collision;
        std::optional<LocalTranslation> translation;
        bool owned, translation_owned, avoidance_owned, collision_owned;
        Json link;
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
        std::optional<NavigationAvoidance> avoidance;
        bool avoidance_owned = false;
        if (version == 1) {
            if (e.has<NavigationAvoidance>())
                throw std::runtime_error(
                    "Legacy navigation recovery cannot restore avoidance state");
        } else if (!entry.at("avoidance").is_null()) {
            const auto& value = entry.at("avoidance");
            detail::validate_components({{"forge.navigation_avoidance", value.at("config")}});
            avoidance = std::get<NavigationAvoidance>(
                navigation_descriptor("forge.navigation_avoidance").decode(value.at("config")));
            avoidance_owned = value.at("owned").get<bool>();
            if (!e.has<NavigationAvoidance>() || avoidance_owned != e.owns<NavigationAvoidance>() ||
                (!avoidance_owned && *avoidance != e.get<NavigationAvoidance>()))
                throw std::runtime_error(
                    "Navigation recovery avoidance ownership/inheritance differs");
        } else if (e.has<NavigationAvoidance>())
            throw std::runtime_error("Navigation recovery avoidance component set differs");
        std::optional<NavigationLinkCollision> collision;
        bool collision_owned = false;
        if (collision_profile) {
            const auto& value = entry.at("collision");
            if (!value.is_null()) {
                detail::validate_components(
                    {{"forge.navigation_link_collision", value.at("config")}});
                collision = link_collision_from_value(value.at("config"));
                collision_owned = value.at("owned").get<bool>();
                if (!e.has<NavigationLinkCollision>() ||
                    collision_owned != e.owns<NavigationLinkCollision>() ||
                    (!collision_owned && *collision != e.get<NavigationLinkCollision>()))
                    throw std::runtime_error(
                        "Navigation recovery collision ownership/inheritance differs");
            } else if (e.has<NavigationLinkCollision>())
                throw std::runtime_error("Navigation recovery collision component set differs");
        } else if (e.has<NavigationLinkCollision>() || entry.contains("collision"))
            throw std::runtime_error(
                "Legacy navigation recovery cannot restore link collision values");
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
        const bool physical_jump = entry.contains("link") && entry.at("link").is_object() &&
                                   entry.at("link").contains("physical_jump");
        if (config.movement != 0 &&
            (version < (config.movement == 2 ? 8u : 7u) || translation || translation_owned ||
             (entry.contains("link") && !(version == 9 && config.movement == 1 && physical_jump))))
            throw std::runtime_error(
                "Character navigation requires its recovery profile without a navigation-owned "
                "pose or links");
        if (owned != e.owns<NavigationAgent>() ||
            (translation &&
             (translation_owned != e.owns<LocalTranslation>() || !e.has<LocalTranslation>())) ||
            (!translation && translation_owned) || (!owned && config != e.get<NavigationAgent>()) ||
            (!translation_owned && translation && *translation != e.get<LocalTranslation>()))
            throw std::runtime_error("Navigation recovery component ownership/inheritance differs");
        if (translation) {
            impl_->context.evaluate_world_transforms();
            impl_->valid_agent(e, config, impl_->context.transform_nodes(), conditions);
        }
        Json link = nullptr;
        if (entry.contains("link")) {
            if (version < 4 || (!translation && !physical_jump))
                throw std::runtime_error(
                    "Navigation traversal recovery requires version 4-8 and a valid pose");
            link = entry.at("link");
            const bool arc = link.contains("arc_height");
            if (!link.is_object() || link.size() != 3u + unsigned(arc) + unsigned(physical_jump) ||
                !link.at("source").get<EntityId>() ||
                (arc && (version < 5 || !link.at("arc_height").is_number())))
                throw std::runtime_error("Invalid navigation traversal recovery identity");
            if (physical_jump) {
                if (version != 9 || config.movement != 1 || !arc ||
                    !link.at("physical_jump").is_boolean() || !link.at("physical_jump").get<bool>())
                    throw std::runtime_error("Invalid physical character jump recovery profile");
                impl_->context.evaluate_world_transforms();
                impl_->valid_agent(e, config, impl_->context.transform_nodes(), conditions);
            }
            if (arc) {
                const auto height = link.at("arc_height").get<float>();
                NavigationLinkCurve::validate_height(height);
                if (height == 0)
                    throw std::runtime_error("Navigation arc recovery requires nonzero height");
            }
            for (const auto* field : {"entry", "exit"}) {
                const auto& endpoint = link.at(field);
                if (!endpoint.is_array() || endpoint.size() != 3)
                    throw std::runtime_error(
                        "Navigation traversal recovery endpoint requires three coordinates");
                for (const auto value : endpoint.get<Double3>())
                    if (!std::isfinite(value))
                        throw std::runtime_error("Invalid navigation traversal recovery endpoint");
            }
        }
        pending.push_back({e, config, avoidance, collision, translation, owned, translation_owned,
                           avoidance_owned, collision_owned, std::move(link)});
    }
    if (expected != seen_entities)
        throw std::runtime_error("Navigation recovery agent set differs");
    if (version == 7 &&
        std::ranges::none_of(pending, [](const auto& p) { return p.agent.movement == 1; }))
        throw std::runtime_error("Navigation recovery7 requires a character movement selection");
    if (version == 8 &&
        std::ranges::none_of(pending, [](const auto& p) { return p.agent.movement == 2; }))
        throw std::runtime_error("Navigation recovery8 requires a2D character movement selection");
    if (version == 9 && std::ranges::none_of(pending, [](const auto& p) {
            return p.link.is_object() && p.link.contains("physical_jump");
        }))
        throw std::runtime_error("Navigation recovery9 requires an active physical character jump");
    // Host-only recovery into an unpublished candidate world. Preserve component ownership;
    // semantic values cover direct gameplay edits hidden by authored property-override intent.
    for (const auto& p : pending) {
        if (p.owned)
            p.entity.set<NavigationAgent>(p.agent);
        if (p.avoidance_owned)
            p.entity.set<NavigationAvoidance>(*p.avoidance);
        if (p.collision_owned)
            p.entity.set<NavigationLinkCollision>(*p.collision);
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
    bool has_crossing = false, has_arc = false;
    impl_->context.evaluate_world_transforms();
    for (const auto& p : pending) {
        if (p.link.is_null())
            continue;
        if (!seen.contains(p.agent.navmesh.id))
            throw std::runtime_error(
                "Navigation traversal recovery requires its exact asset revision");
        auto& asset = impl_->get(p.agent.navmesh);
        const auto source = p.link.at("source").get<EntityId>();
        const auto entry = p.link.at("entry").get<Double3>(),
                   exit = p.link.at("exit").get<Double3>();
        const auto arc_height = p.link.value("arc_height", 0.f);
        if (!asset.query->link_available(source, entry, exit, arc_height))
            throw std::runtime_error(
                "Navigation traversal recovery differs from the admitted oriented link");
        const auto position = p.entity.get<WorldTransform>().affine.point({0, 0, 0});
        const bool physical_jump = p.link.value("physical_jump", false);
        if (physical_jump) {
            const auto physics = impl_->context.services().available(Capability::Physics)
                                     ? impl_->context.services().physics()
                                     : nullptr;
            const auto ref = impl_->context.reference(p.entity.id());
            if (!physics || !ref)
                throw std::runtime_error(
                    "Physical jump recovery requires restored character physics");
            const auto character = physics->character(*ref);
            if (distance(position, {character.position.x, character.position.y,
                                    character.position.z}) > 1e-6 ||
                (character.ground == CharacterGround::OnGround &&
                 character.velocity[1] <= character.ground_velocity[1]))
                throw std::runtime_error(
                    "Physical jump recovery requires its active native character");
        } else if (arc_height != 0) {
            const NavigationLinkCurve curve(entry, exit, arc_height);
            const auto t = curve.parameter(position);
            if (t < -1e-9 || t > 1 + 1e-9 ||
                distance(position, curve.point(std::clamp(t, 0., 1.))) > 1e-6)
                throw std::runtime_error("Navigation traversal recovery pose is off its arc");
            has_arc = true;
        } else {
            double length_squared = 0, projection = 0;
            for (unsigned i = 0; i < 3; ++i) {
                length_squared += (exit[i] - entry[i]) * (exit[i] - entry[i]);
                projection += (position[i] - entry[i]) * (exit[i] - entry[i]);
            }
            if (length_squared < .000001 || projection < -.000001 ||
                projection > length_squared + .000001)
                throw std::runtime_error("Navigation traversal recovery pose is outside its span");
            Double3 closest;
            for (unsigned i = 0; i < 3; ++i)
                closest[i] = entry[i] + (exit[i] - entry[i]) * projection / length_squared;
            if (distance(position, closest) > .001)
                throw std::runtime_error("Navigation traversal recovery pose is off its span");
        }
        AgentState state;
        state.config = p.agent;
        state.path = {entry, exit};
        state.links = {{source, 0, 1, arc_height}};
        state.crossing = 0;
        state.physical_jump = physical_jump;
        state.last = position;
        state.query_generation = asset.query->generation();
        state.replan_after_link = true;
        impl_->agents.emplace(p.entity.id(), std::move(state));
        has_crossing = true;
    }
    if (version == 4 && !has_crossing)
        throw std::runtime_error("Navigation recovery version 4 requires an active traversal");
    if (version == 5 && !has_arc)
        throw std::runtime_error("Navigation recovery version 5 requires an active arc");
    if (collision_profile &&
        std::ranges::none_of(pending, [](const auto& p) { return p.collision.has_value(); }))
        throw std::runtime_error("Navigation recovery version 6 requires link collision values");
    impl_->behaviors.restore(version >= 3 ? data.at("behaviors") : Json::array());
}
EngineModule navigation_module(std::filesystem::path project) {
    auto m = navigation_schema_module();
    m.runtime_roles = role_mask(WorldRole::Runtime);
    m.allowed_services = capability(Capability::Diagnostics) | capability(Capability::Profiling) |
                         capability(Capability::Navigation) | capability(Capability::Resources) |
                         capability(Capability::Physics);
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
