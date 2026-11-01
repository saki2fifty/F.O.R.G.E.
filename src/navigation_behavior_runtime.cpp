#include "navigation_behavior_runtime.hpp"
#include "builtins.hpp"
#include "json_output_text.hpp"
#include <algorithm>
#include <cmath>
#include <set>

namespace forge {
namespace {
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
bool same_intent(const NavigationAgent& a, const NavigationAgent& b) {
    return a.has_destination == b.has_destination && a.destination_x == b.destination_x &&
           a.destination_y == b.destination_y && a.destination_z == b.destination_z;
}
Double3 position(flecs::entity e) {
    require(e && e.has<WorldTransform>() && e.get<WorldTransform>().resolved,
            "Behavior requires a resolved spatial transform");
    const auto point = e.get<WorldTransform>().affine.point({0, 0, 0});
    for (double v : point)
        require(std::isfinite(v), "Behavior transform is not finite");
    return point;
}
} // namespace
NavigationBehaviorPlayback::NavigationBehaviorPlayback(WorldContext& context)
    : context_(context), query_(context.world().query<const NavigationBehavior>()) {}
NavigationBehaviorPlayback::~NavigationBehaviorPlayback() { clear_graphs(); }
void NavigationBehaviorPlayback::check() const {
    if (!context_.on_owner_thread() || context_.world().is_readonly())
        throw std::runtime_error("Behavior requires its writable runtime owner");
}
void NavigationBehaviorPlayback::clear_graphs() noexcept {
    for (auto& [id, graph] : graphs_) {
        (void)id;
        graph.lease = {};
        if (auto owner = resources_.lock(); owner && graph.token)
            try {
                owner->release(graph.token);
            } catch (...) { /* Resource owner already stopped. */
            }
    }
    graphs_.clear();
    resources_.reset();
}
void NavigationBehaviorPlayback::bind(Scene* scene) {
    check();
    for (auto& [id, player] : players_) {
        auto e = context_.world().entity(id);
        release_intent(e, player);
        if (e.is_alive())
            e.remove<NavigationBehaviorCursor>();
    }
    players_.clear();
    global_error_.clear();
    clear_graphs();
    scene_ = scene;
}
std::vector<flecs::entity_t> NavigationBehaviorPlayback::controllers() const {
    std::vector<flecs::entity_t> result;
    if (!scene_)
        return result;
    query_.each([&](flecs::entity e, const NavigationBehavior&) {
        if (!e.has(flecs::Prefab) && e.owns<PersistentEntityId>() && result.size() < 65 &&
            scene_->entity(e.get<PersistentEntityId>().value.str()) == e)
            result.push_back(e.id());
    });
    require(result.size() <= 64, "Navigation behavior exceeds 64 Scene controllers");
    return result;
}
void NavigationBehaviorPlayback::release_intent(flecs::entity e, Player& player) {
    if (e.is_alive() && e.has<NavigationAgent>() && player.issued &&
        same_intent(e.get<NavigationAgent>(), *player.issued)) {
        auto config = e.get<NavigationAgent>();
        if (config.has_destination) {
            config.has_destination = false;
            e.set<NavigationAgent>(config);
        }
    }
    player.issued.reset();
}
void NavigationBehaviorPlayback::stop(flecs::entity e, Player& player) {
    if (!e.is_alive() || !e.has<NavigationAgent>())
        return;
    auto config = e.get<NavigationAgent>();
    if (config.has_destination) {
        config.has_destination = false;
        e.set<NavigationAgent>(config);
    }
    player.issued = config;
}
bool NavigationBehaviorPlayback::prepare(bool fail_on_error,
                                         std::span<const AssetId> extra_graphs) {
    check();
    const auto entities = controllers();
    const std::set<flecs::entity_t> alive(entities.begin(), entities.end());
    std::set<AssetId> configured, needed;
    for (auto id : entities) {
        const auto& config = context_.world().entity(id).get<NavigationBehavior>();
        if (config.graph.id)
            configured.insert(config.graph.id);
    }
    require(configured.size() <= 8, "Navigation behavior exceeds eight configured graph assets");
    for (auto id : extra_graphs) {
        require(configured.contains(id), "Required behavior graph is not configured in this Scene");
        needed.insert(id);
    }
    for (auto at = players_.begin(); at != players_.end();) {
        if (alive.contains(at->first)) {
            ++at;
            continue;
        }
        auto e = context_.world().entity(at->first);
        release_intent(e, at->second);
        if (e.is_alive())
            e.remove<NavigationBehaviorCursor>();
        at = players_.erase(at);
    }
    auto resources = context_.services().available(Capability::Resources)
                         ? context_.services().resources()
                         : std::shared_ptr<RuntimeResourceService>{};
    if (resources_.lock() != resources) {
        clear_graphs();
        resources_ = resources;
    }
    for (auto at = graphs_.begin(); at != graphs_.end();) {
        if (configured.contains(at->first)) {
            ++at;
            continue;
        }
        at->second.lease = {};
        if (resources && at->second.token)
            resources->release(at->second.token);
        at = graphs_.erase(at);
    }
    for (auto id : entities) {
        auto e = context_.world().entity(id);
        auto& player = players_[id];
        const auto config = e.get<NavigationBehavior>();
        if (player.config.graph.id != config.graph.id) {
            release_intent(e, player);
            e.remove<NavigationBehaviorCursor>();
            player.cooked_digest.clear();
            player.revision.clear();
            player.step_error.clear();
            player.sampled = false;
        }
        player.config = config;
        player.preparation_error.clear();
        player.pending = false;
        if (!config.enabled) {
            release_intent(e, player);
            player.step_error.clear();
            continue;
        }
        if (!e.has<NavigationAgent>())
            player.preparation_error = "Navigation Behavior requires Navigation Agent";
        else if (!config.graph.id)
            player.preparation_error = "Navigation Behavior requires a published graph";
        else
            needed.insert(config.graph.id);
    }
    for (auto id : needed) {
        auto& graph = graphs_[id];
        try {
            require(bool(resources), "Navigation Behavior requires runtime Resources");
            if (!graph.token)
                graph.token = resources->request(RuntimeResourceKind::NavigationBehavior, id);
            graph.status = resources->inspect(graph.token);
            graph.lease = resources->navigation_behavior(graph.token);
        } catch (const std::exception& error) {
            graph.status.state = "failed";
            graph.status.diagnostic = error.what();
            graph.lease = {};
        }
    }
    bool ready = true;
    std::string first_error;
    for (auto id : entities) {
        auto e = context_.world().entity(id);
        auto& player = players_.at(id);
        if (!player.config.enabled && !needed.contains(player.config.graph.id))
            continue;
        if (player.preparation_error.empty()) {
            auto& graph = graphs_.at(player.config.graph.id);
            if (graph.lease) {
                if (player.cooked_digest != graph.lease->cooked_digest) {
                    e.set<NavigationBehaviorCursor>({});
                    player.cooked_digest = graph.lease->cooked_digest;
                    player.pending = ecs_is_deferred(context_.world());
                } else if (!e.owns<NavigationBehaviorCursor>()) {
                    e.set<NavigationBehaviorCursor>({});
                    player.pending = ecs_is_deferred(context_.world());
                }
                player.revision = graph.lease.identity().revision;
            } else {
                player.pending = true;
                if (graph.status.state == "failed")
                    player.preparation_error = graph.status.diagnostic;
            }
        }
        if (!player.preparation_error.empty()) {
            ready = false;
            if (first_error.empty())
                first_error = player.preparation_error;
        }
        ready &= !player.pending;
    }
    if (fail_on_error && !first_error.empty())
        throw std::runtime_error("Navigation behavior: " + first_error);
    return ready;
}
flecs::entity NavigationBehaviorPlayback::resolve(EntityRef ref) const {
    if (!scene_ || ref.scene != scene_->asset_id() || !ref.entity)
        return {};
    const auto found = context_.resolve(ref);
    if (found.state != WorldContext::ResolveState::Available)
        return {};
    const auto e = context_.world().entity(found.entity);
    return scene_->entity(ref.entity.str()) == e ? e : flecs::entity{};
}
void NavigationBehaviorPlayback::report(flecs::entity_t id, Player& player) {
    const auto error =
        player.preparation_error.empty() ? player.step_error : player.preparation_error;
    if (error == player.reported_error)
        return;
    player.reported_error = error;
    if (error.empty() || !context_.services().available(Capability::Diagnostics))
        return;
    Diagnostic value{Severity::Warning, "navigation.behavior", error, {}};
    value.context.module = "forge.navigation";
    value.context.world_role = world_role_name(context_.role());
    value.context.source = "NavigationBehaviorAsset";
    value.context.tick = player.tick;
    if (const auto ref = context_.reference(id))
        value.context.entity = ref->entity;
    context_.services().emit(std::move(value));
}
std::optional<Double3> NavigationBehaviorPlayback::goal(const NavigationBehaviorState& state,
                                                        const Frame& frame) const {
    if (state.action == NavigationBehaviorAction::Wait)
        return {};
    Double3 point;
    if (state.action == NavigationBehaviorAction::FollowTarget) {
        require(frame.target.has_value(), "Follow target requires a resolved Scene target");
        point = *frame.target;
    } else {
        require(state.action == NavigationBehaviorAction::MoveToPoint,
                "Invalid navigation behavior action");
        point = state.point;
        if (frame.plane == NavigationPlane::PlanarXY)
            point[2] = 0;
        if (frame.anchor_required) {
            require(frame.anchor.has_value(), "Point action requires a resolved Scene anchor");
            point = frame.anchor->point(point);
        }
    }
    if (frame.plane == NavigationPlane::PlanarXY)
        point[2] = frame.actor[2];
    for (double v : point)
        require(std::isfinite(v) && std::abs(v) <= 4096,
                "Behavior destination exceeds the navigation world bounds");
    return point;
}
void NavigationBehaviorPlayback::apply_intent(flecs::entity e, Player& player,
                                              const std::optional<Double3>& destination) {
    require(e.has<NavigationAgent>(), "Navigation Behavior requires Navigation Agent");
    auto next = e.get<NavigationAgent>();
    next.has_destination = bool(destination);
    if (destination) {
        next.destination_x = (*destination)[0];
        next.destination_y = (*destination)[1];
        next.destination_z = (*destination)[2];
    }
    if (next != e.get<NavigationAgent>())
        e.set<NavigationAgent>(next);
    player.issued = next;
}
void NavigationBehaviorPlayback::begin_tick(double dt, std::uint64_t tick,
                                            const PlaneLookup& plane_for) {
    require(std::isfinite(dt) && dt > 0 && dt <= 1, "Behavior requires positive fixed delta <= 1");
    try {
        (void)prepare();
        global_error_.clear();
    } catch (const std::exception& error) {
        global_error_ = error.what();
        throw;
    }
    // Snapshot every controller's resolved inputs before any agent moves. Only
    // intent is applied here; the one cursor-clock/transition step follows movement.
    for (auto& [id, player] : players_) {
        auto e = context_.world().entity(id);
        player.tick = tick;
        player.sampled = false;
        player.frame.reset();
        if (!player.config.enabled) {
            player.step_error.clear();
            continue;
        }
        // New/reset cursor storage can be queued by the immediate pipeline
        // system's defer scope. Hold intent until Flecs adopts it; never read
        // absent storage or advance an old graph's cursor before that merge.
        if (!e.owns<NavigationBehaviorCursor>())
            player.pending = true;
        if (player.pending || !player.preparation_error.empty()) {
            stop(e, player);
            player.step_error.clear();
            report(id, player);
            continue;
        }
        try {
            const auto& graph = graphs_.at(player.config.graph.id).lease.get();
            detail::validate_reflected_value(e, player.config);
            Frame frame{};
            frame.plane = plane_for(e.get<NavigationAgent>().navmesh);
            frame.actor = position(e);
            frame.cursor = e.get<NavigationBehaviorCursor>();
            validate_navigation_behavior_cursor(graph, frame.cursor);
            const auto target = resolve(player.config.target);
            if (target && target.has<WorldTransform>() && target.get<WorldTransform>().resolved)
                frame.target = position(target);
            frame.anchor_required =
                bool(player.config.anchor.entity) || bool(player.config.anchor.scene);
            if (frame.anchor_required) {
                const auto anchor = resolve(player.config.anchor);
                if (anchor && anchor.has<WorldTransform>() && anchor.get<WorldTransform>().resolved)
                    frame.anchor = anchor.get<WorldTransform>().affine;
            }
            player.frame = frame;
            const auto& state = graph.state(frame.cursor.state ? frame.cursor.state : graph.entry);
            player.frame->goal = goal(state, frame);
            apply_intent(e, player, player.frame->goal);
            player.step_error.clear();
        } catch (const std::exception& error) {
            stop(e, player);
            player.step_error = error.what();
            // A missing target/anchor action can still leave through a valid
            // transition at finish_tick. Invalid graph/cursor/pose has no frame.
            if (!player.frame)
                report(id, player);
        }
    }
}
void NavigationBehaviorPlayback::finish_tick(double dt, const AgentLookup& observation) {
    check();
    for (auto& [id, player] : players_) {
        if (!player.frame)
            continue;
        auto e = context_.world().entity(id);
        const auto frame = std::move(*player.frame);
        player.frame.reset();
        if (!e.is_alive() || !e.has<NavigationBehavior>() ||
            e.get<NavigationBehavior>() != player.config || !e.has<NavigationBehaviorCursor>() ||
            e.get<NavigationBehaviorCursor>() != frame.cursor) {
            release_intent(e, player);
            continue; // A native edit wins; retry from that value at the next boundary.
        }
        try {
            const auto& graph = graphs_.at(player.config.graph.id).lease.get();
            NavigationBehaviorInputs input;
            if (frame.target) {
                const auto& p = *frame.target;
                input.target_distance =
                    frame.plane == NavigationPlane::PlanarXY
                        ? std::hypot(frame.actor[0] - p[0], frame.actor[1] - p[1])
                        : std::hypot(frame.actor[0] - p[0], frame.actor[1] - p[1],
                                     frame.actor[2] - p[2]);
            }
            const auto ref = context_.reference(id);
            const auto current = ref ? observation(*ref) : std::nullopt;
            const auto agent = e.get<NavigationAgent>();
            input.arrived =
                frame.goal && agent.has_destination && current && current->sampled &&
                current->tick == player.tick && current->phase == NavigationAgentPhase::Arrived &&
                agent.destination_x == (*frame.goal)[0] &&
                agent.destination_y == (*frame.goal)[1] && agent.destination_z == (*frame.goal)[2];
            auto cursor = frame.cursor;
            const auto& state = advance_navigation_behavior(graph, cursor, input, dt);
            const auto destination = goal(state, frame);
            apply_intent(e, player, destination);
            if (e.get<NavigationBehaviorCursor>() != cursor)
                e.set<NavigationBehaviorCursor>(cursor);
            player.step_error.clear();
            player.sampled = true;
        } catch (const std::exception& error) {
            stop(e, player);
            player.step_error = error.what();
        }
        report(id, player);
    }
}
Json NavigationBehaviorPlayback::debug(flecs::entity_t id) const {
    if (!context_.on_owner_thread() || !scene_)
        return nullptr;
    const auto found = players_.find(id);
    if (found == players_.end()) {
        if (!global_error_.empty() && context_.world().entity(id).has<NavigationBehavior>())
            return {{"phase", "Failed"},
                    {"diagnostic", detail::json_output_text(global_error_.substr(0, 2048))}};
        return nullptr;
    }
    const auto& p = found->second;
    const auto e = context_.world().entity(id);
    const auto cursor = e.has<NavigationBehaviorCursor>() ? e.get<NavigationBehaviorCursor>()
                                                          : NavigationBehaviorCursor{};
    const auto graph = graphs_.find(p.config.graph.id);
    std::string name;
    if (cursor.state && graph != graphs_.end() && graph->second.lease)
        for (const auto& state : graph->second.lease->states)
            if (state.id == cursor.state)
                name = state.name;
    const auto error = !global_error_.empty()        ? global_error_
                       : p.preparation_error.empty() ? p.step_error
                                                     : p.preparation_error;
    const char* phase = !p.config.enabled ? "Disabled"
                        : !error.empty()  ? "Failed"
                        : p.pending       ? "Pending"
                        : p.sampled       ? "Running"
                                          : "Ready";
    Json result = {{"phase", phase},
                   {"graph", p.config.graph.id ? Json(p.config.graph.id) : Json()},
                   {"state", cursor.state},
                   {"state_name", name},
                   {"elapsed", std::isfinite(cursor.elapsed) ? Json(cursor.elapsed) : Json()},
                   {"last_transition", cursor.last_transition},
                   {"transitions", cursor.transitions},
                   {"tick", p.tick},
                   {"sampled", p.sampled},
                   {"diagnostic", detail::json_output_text(error.substr(0, 2048))}};
    if (graph != graphs_.end()) {
        const auto& g = graph->second;
        result["resource_state"] = g.status.state;
        result["requested_revision"] = g.status.requested_revision;
        result["retained_revision"] = g.status.retained_revision;
        result["resource_diagnostic"] =
            detail::json_output_text(g.status.diagnostic.substr(0, 2048));
        result["retained_last_good"] =
            bool(g.lease) && (g.status.state == "failed" ||
                              g.status.requested_revision != g.status.retained_revision);
    }
    return result;
}
} // namespace forge
