#include "asset_bytes.hpp"
#include "bounded_json.hpp"
#include <algorithm>
#include <forge/application_state_activation.hpp>
#include <forge/application_state_components.hpp>
#include <forge/application_states.hpp>
#include <forge/world.hpp>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace forge {
namespace {
constexpr std::size_t none = std::numeric_limits<std::size_t>::max();
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
struct CompiledDomain {
    ApplicationStateDomainDefinition definition;
    flecs::entity_t entity = 0;
    std::vector<flecs::entity_t> states;
    std::map<std::string, std::size_t, std::less<>> indices;
    std::vector<std::size_t> parents;
    std::uint64_t revision = 0;
};
struct RuntimeStates {
    std::vector<CompiledDomain> domains;
    std::map<std::string, std::size_t, std::less<>> indices;
    std::vector<std::size_t> order;
    std::string definition_digest;
    AssetId source;
    bool advancing = false, initialized = false;
};
struct ApplicationStatesStorage {
    // Stable across native table moves caused by user observers.
    std::shared_ptr<RuntimeStates> runtime;
    std::function<void()> retired;
};
const ApplicationStatesStorage* storage(const flecs::world& world) {
    const auto id =
        ecs_lookup_path_w_sep(world, 0, "::forge.application.Storage", "::", "::", false);
    return id ? static_cast<const ApplicationStatesStorage*>(ecs_get_id(world, id, id)) : nullptr;
}
bool condition_active(const flecs::world& world, const ApplicationStateGate& value) {
    value.validate();
    if (!value.enabled)
        return true;
    const auto* slot = storage(world);
    require(slot && slot->runtime,
            "Enabled State Condition requires configured Application States");
    const auto& runtime = *slot->runtime;
    const auto at = runtime.indices.find(value.domain);
    require(at != runtime.indices.end(), "State Condition has an unknown domain key");
    const auto& domain = runtime.domains[at->second];
    const auto state = domain.indices.find(value.state);
    require(state != domain.indices.end(), "State Condition has an unknown state key");
    return runtime.initialized &&
           world.entity(domain.entity).has<ApplicationStateMember>(domain.states[state->second]);
}
void owner_boundary(const flecs::world& world) {
    require(ecs_get_world(world) == world.c_ptr() && !ecs_stage_is_readonly(world) &&
                !ecs_is_deferred(world),
            "Application state update requires the mutable owner boundary");
}
std::size_t leaf(const CompiledDomain& domain, flecs::entity_t target) {
    if (!target)
        return none;
    const auto at = std::find(domain.states.begin(), domain.states.end(), target);
    require(at != domain.states.end(), "Application state relationship has an unknown target");
    return static_cast<std::size_t>(at - domain.states.begin());
}
std::vector<std::size_t> path(const CompiledDomain& domain, std::size_t state) {
    std::vector<std::size_t> result;
    for (; state != none; state = domain.parents[state])
        result.push_back(state);
    return result;
}
void publish_active(flecs::world& world, const CompiledDomain& domain, std::size_t state) {
    auto entity = world.entity(domain.entity);
    entity.remove<ApplicationStateActive>(flecs::Wildcard);
    entity.remove<ApplicationStateMember>(flecs::Wildcard);
    if (state != none) {
        entity.add<ApplicationStateActive>(domain.states[state]);
        for (const auto member : path(domain, state))
            entity.add<ApplicationStateMember>(domain.states[member]);
    }
}
std::string definition_digest(const ApplicationStatesDefinition& definition, AssetId source) {
    using Json = nlohmann::json;
    Json domains = Json::array();
    const auto condition = [](const ApplicationStateCondition& c) {
        return Json{{"domain", c.domain}, {"state", c.state}};
    };
    for (const auto& domain : definition.domains) {
        Json states = Json::array(), rules = Json::array();
        for (const auto& state : domain.states)
            states.push_back(
                {{"key", state.key}, {"label", state.label}, {"parent", state.parent}});
        for (const auto& rule : domain.rules) {
            Json all = Json::array();
            for (const auto& c : rule.all)
                all.push_back(condition(c));
            rules.push_back({{"state", rule.state}, {"all", std::move(all)}});
        }
        domains.push_back(
            {{"key", domain.key},
             {"label", domain.label},
             {"initial", domain.initial},
             {"computed", domain.computed},
             {"states", std::move(states)},
             {"rules", std::move(rules)},
             {"enabled_when", domain.enabled_when ? condition(*domain.enabled_when) : Json()}});
    }
    const auto text = Json{{"format", "forge.application-states.runtime.v1"},
                           {"source", source ? Json(source) : Json()},
                           {"domains", std::move(domains)}}
                          .dump();
    return asset_detail::content_digest(std::as_bytes(std::span(text)));
}
bool matches(const RuntimeStates& runtime, const std::vector<std::size_t>& values,
             const ApplicationStateCondition& condition) {
    const auto index = runtime.indices.at(condition.domain);
    const auto& domain = runtime.domains[index];
    const auto wanted = domain.indices.at(condition.state);
    for (auto state = values[index]; state != none; state = domain.parents[state])
        if (state == wanted)
            return true;
    return false;
}
struct StateRequest {
    std::size_t domain, state;
};
StateRequest select_request(const flecs::world& world, const RuntimeStates& runtime,
                            std::string_view domain_key, std::string_view state_key,
                            std::optional<std::uint64_t> expected_revision) {
    const auto at = runtime.indices.find(domain_key);
    require(at != runtime.indices.end(), "Unknown application state domain");
    const auto& domain = runtime.domains[at->second];
    require(!expected_revision || *expected_revision == domain.revision,
            "Stale application state revision; read the current domain first");
    require(!domain.definition.computed, "Computed application states cannot be requested");
    const auto state = domain.indices.find(state_key);
    require(state != domain.indices.end(), "Unknown application state");
    require(!domain.definition.enabled_when ||
                world.entity(domain.entity).target<ApplicationStateActive>(),
            "Subordinate application state domain is inactive");
    return {at->second, state->second};
}
bool advance_application_states_impl(flecs::world&, std::optional<StateRequest>);
} // namespace

struct ApplicationStateActivation::Impl {
    std::unordered_map<flecs::entity_t, bool> conditions, resolved;
    std::vector<flecs::entity_t> path;
};
void validate_application_state_gate(WorldContext& context, const ApplicationStateGate& value) {
    require(context.on_owner_thread(),
            "State Condition validation requires the world owner thread");
    if (context.role() == WorldRole::Runtime)
        (void)condition_active(context.world(), value);
    else
        value.validate();
}
ApplicationStateActivation::ApplicationStateActivation(WorldContext& context) : context_(&context) {
    require(context.on_owner_thread(), "State Condition capture requires the world owner thread");
    if (context.role() != WorldRole::Runtime)
        return;
    auto& world = context.world();
    const auto type =
        ecs_lookup_path_w_sep(world, 0, "::forge.application_state_gate", "::", "::", false);
    if (!type || !ecs_count_id(world, type))
        return;
    std::vector<std::pair<flecs::entity_t, ApplicationStateGate>> values;
    bool overflow = false;
    auto query = world.query_builder<const ApplicationStateGate>()
                     .query_flags(EcsQueryMatchDisabled)
                     .build();
    query.each([&](flecs::entity entity, const ApplicationStateGate& value) {
        if (values.size() == 10000) {
            overflow = true;
            return;
        }
        values.emplace_back(entity.id(), value);
    });
    require(!overflow, "State Condition capture exceeds 10000 entities");
    auto result = std::make_unique<Impl>();
    for (const auto& [entity, value] : values) {
        const auto active = condition_active(world, value);
        if (!value.enabled)
            continue;
        result->conditions.emplace(entity, active);
    }
    if (!result->conditions.empty())
        impl_ = std::move(result);
}
ApplicationStateActivation::~ApplicationStateActivation() = default;
ApplicationStateActivation::ApplicationStateActivation(ApplicationStateActivation&& other) noexcept
    : context_(std::exchange(other.context_, nullptr)), impl_(std::move(other.impl_)) {}
ApplicationStateActivation&
ApplicationStateActivation::operator=(ApplicationStateActivation&& other) noexcept {
    if (this != &other) {
        context_ = std::exchange(other.context_, nullptr);
        impl_ = std::move(other.impl_);
    }
    return *this;
}
bool ApplicationStateActivation::active(flecs::entity_t entity) {
    if (!context_)
        return false;
    require(context_->on_owner_thread(), "State Condition read requires the world owner thread");
    auto& world = context_->world();
    if (!entity || !world.is_alive(entity))
        return false;
    if (!impl_)
        return true;
    auto& value = *impl_;
    value.path.clear();
    bool active = true;
    for (auto current = entity; current; current = ecs_get_target(world, current, EcsChildOf, 0)) {
        const auto known = value.resolved.find(current);
        if (known != value.resolved.end()) {
            active = known->second;
            break;
        }
        if (value.path.size() == 10001) {
            active = false;
            break;
        }
        value.path.push_back(current);
    }
    for (auto at = value.path.rbegin(); at != value.path.rend(); ++at) {
        const auto own = value.conditions.find(*at);
        active = active && (own == value.conditions.end() || own->second);
        value.resolved.emplace(*at, active);
    }
    return active;
}

void register_application_states(flecs::world& world) {
    owner_boundary(world);
    struct Scope {
        flecs::world& world;
        flecs::entity_t previous;
        explicit Scope(flecs::world& w) : world(w), previous(ecs_set_scope(w, 0)) {}
        ~Scope() { ecs_set_scope(world, previous); }
    } scope(world);
    world.component<ApplicationStateDomainTag>("forge.application.Domain")
        .add(flecs::OnInstantiate, flecs::DontInherit);
    world.component<ApplicationStateActive>("forge.application.Active")
        .add(flecs::Exclusive)
        .add(flecs::OnInstantiate, flecs::DontInherit);
    world.component<ApplicationStateMember>("forge.application.Member")
        .add(flecs::OnInstantiate, flecs::DontInherit);
    world.component<ApplicationStateRequested>("forge.application.Requested")
        .add(flecs::Exclusive)
        .add(flecs::OnInstantiate, flecs::DontInherit);
    world.component<ApplicationStateLifetime>("forge.application.Lifetime")
        .add(flecs::Exclusive)
        .add(flecs::OnInstantiate, flecs::DontInherit);
    world.component<ApplicationStateEntered>("forge.application.Entered");
    world.component<ApplicationStateExited>("forge.application.Exited");
    world.component<ApplicationStatesStorage>("forge.application.Storage");
}
void configure_application_states(flecs::world& world, const ApplicationStatesDefinition& value,
                                  AssetId source) {
    owner_boundary(world);
    require(!storage(world) || !storage(world)->runtime,
            "Application states are already configured for this World");
    value.validate();
    register_application_states(world);
    auto runtime = std::make_shared<RuntimeStates>();
    runtime->definition_digest = definition_digest(value, source);
    runtime->source = source;
    for (const auto& definition : value.domains) {
        CompiledDomain domain;
        domain.definition = definition;
        domain.entity = application_state_domain(world, definition.key);
        for (const auto& state : definition.states) {
            domain.indices.emplace(state.key, domain.states.size());
            domain.states.push_back(application_state_entity(world, definition.key, state.key));
        }
        for (const auto& state : definition.states)
            domain.parents.push_back(state.parent.empty() ? none : domain.indices.at(state.parent));
        runtime->indices.emplace(definition.key, runtime->domains.size());
        runtime->domains.push_back(std::move(domain));
    }
    std::vector<bool> visited(runtime->domains.size());
    const std::function<void(std::size_t)> visit = [&](std::size_t i) {
        if (visited[i])
            return;
        visited[i] = true;
        const auto& definition = runtime->domains[i].definition;
        if (definition.enabled_when)
            visit(runtime->indices.at(definition.enabled_when->domain));
        for (const auto& rule : definition.rules)
            for (const auto& condition : rule.all)
                visit(runtime->indices.at(condition.domain));
        runtime->order.push_back(i);
    };
    for (std::size_t i = 0; i < runtime->domains.size(); ++i)
        visit(i);
    runtime->advancing = true;
    const auto retired = storage(world) ? storage(world)->retired : std::function<void()>{};
    world.set<ApplicationStatesStorage>({runtime, retired});
    for (const auto& domain : runtime->domains)
        world.entity(domain.entity).add<ApplicationStateDomainTag>();
    runtime->advancing = false;
}
void request_application_state(flecs::world& world, std::string_view domain_key,
                               std::string_view state_key,
                               std::optional<std::uint64_t> expected_revision) {
    const auto* slot = storage(world);
    require(slot && slot->runtime, "Application states are not configured");
    const auto& runtime = *slot->runtime;
    const auto selected = select_request(world, runtime, domain_key, state_key, expected_revision);
    const auto& domain = runtime.domains[selected.domain];
    world.entity(domain.entity).add<ApplicationStateRequested>(domain.states[selected.state]);
}
void bind_application_state_lifetime(flecs::world& world, flecs::entity_t root,
                                     std::string_view domain_key, std::string_view state_key,
                                     std::optional<std::uint64_t> expected_revision) {
    owner_boundary(world);
    const auto* slot = storage(world);
    require(slot && slot->runtime && slot->runtime->initialized,
            "State lifetime requires an initialized application-state definition");
    const auto& runtime = *slot->runtime;
    const auto at = runtime.indices.find(domain_key);
    require(at != runtime.indices.end(), "Unknown lifetime application-state domain");
    const auto& domain = runtime.domains[at->second];
    require(!expected_revision || *expected_revision == domain.revision,
            "Stale application state revision; read the current domain first");
    const auto state = domain.indices.find(state_key);
    require(state != domain.indices.end(), "Unknown lifetime application state");
    const auto target = domain.states[state->second];
    require(world.entity(domain.entity).has<ApplicationStateMember>(target),
            "State lifetime requires an active state or ancestor");
    require(root && world.is_alive(root) && !world.entity(root).has(flecs::Prefab),
            "State lifetime requires a live runtime root");
    auto entity = world.entity(root);
    const auto previous = entity.target<ApplicationStateLifetime>();
    require(!previous || previous.id() == target,
            "A runtime root's state lifetime cannot be rebound");
    if (!previous)
        entity.add<ApplicationStateLifetime>(target);
}
nlohmann::json application_state_lifetime(const flecs::world& world, flecs::entity_t root) {
    if (!root || !world.is_alive(root))
        return nullptr;
    const auto* slot = storage(world);
    if (!slot || !slot->runtime)
        return nullptr;
    const auto target = world.entity(root).target<ApplicationStateLifetime>();
    if (!target)
        return nullptr;
    for (const auto& domain : slot->runtime->domains)
        for (std::size_t i = 0; i < domain.states.size(); ++i)
            if (domain.states[i] == target.id())
                return {{"domain", domain.definition.key},
                        {"state", domain.definition.states[i].key}};
    throw std::runtime_error("State lifetime has an unknown semantic target");
}
std::function<void()> exchange_application_state_retirement_handler(flecs::world& world,
                                                                    std::function<void()> handler) {
    owner_boundary(world);
    register_application_states(world);
    const auto* slot = storage(world);
    require(!slot || !slot->runtime || !slot->runtime->advancing,
            "State retirement handler cannot change during a transition");
    auto value = slot ? *slot : ApplicationStatesStorage{};
    auto previous = std::move(value.retired);
    value.retired = std::move(handler);
    world.set<ApplicationStatesStorage>(value);
    return previous;
}
nlohmann::json application_states_status(const flecs::world& world, bool choices) {
    const auto* slot = storage(world);
    if (!slot || !slot->runtime)
        return nullptr;
    const auto& runtime = *slot->runtime;
    auto rows = nlohmann::json::array();
    const auto observed = observe_application_states(world);
    for (std::size_t i = 0; i < observed.size(); ++i) {
        const auto& view = observed[i];
        const auto& definition = runtime.domains[i].definition;
        std::string active_label;
        if (!view.active.empty())
            active_label = definition.states[runtime.domains[i].indices.at(view.active)].label;
        nlohmann::json row{{"key", view.domain},          {"label", view.label},
                           {"active", view.active},       {"active_label", std::move(active_label)},
                           {"requested", view.requested}, {"membership", view.membership},
                           {"revision", view.revision},   {"computed", view.computed}};
        if (choices) {
            row["states"] = nlohmann::json::array();
            for (const auto& state : definition.states)
                row["states"].push_back(
                    {{"key", state.key}, {"label", state.label}, {"parent", state.parent}});
        }
        rows.push_back(std::move(row));
    }
    return {{"version", 1},
            {"source_asset", runtime.source ? nlohmann::json(runtime.source) : nlohmann::json()},
            {"definition_digest", runtime.definition_digest},
            {"initialized", runtime.initialized},
            {"domains", std::move(rows)}};
}
nlohmann::json control_application_states(flecs::world& world, const nlohmann::json& command) {
    owner_boundary(world);
    const auto* slot = storage(world);
    require(slot && slot->runtime && slot->runtime->initialized && !slot->runtime->advancing,
            "Application state controls require an initialized definition");
    require(command.is_object() && command.size() <= 4, "Invalid application state command");
    for (const auto& [key, unused] : command.items()) {
        (void)unused;
        require(key == "operation" || key == "domain" || key == "state" ||
                    key == "expected_revision",
                "Unknown application state command field");
    }
    require(!command.contains("operation") || command.at("operation") == "application_state",
            "Invalid application state operation");
    std::optional<std::uint64_t> expected;
    if (command.contains("expected_revision")) {
        require(command.at("expected_revision").is_number_unsigned(),
                "State revision must be an unsigned integer");
        expected = command.at("expected_revision").get<std::uint64_t>();
    }
    const auto selected =
        select_request(world, *slot->runtime, command.at("domain").get<std::string>(),
                       command.at("state").get<std::string>(), expected);
    // Overlay host input in the copied plan. Counter/derived-domain validation
    // must succeed before replacing any existing native pending intent.
    advance_application_states_impl(world, selected);
    return application_states_status(world);
}
namespace {
bool advance_application_states_impl(flecs::world& world,
                                     std::optional<StateRequest> pending_override) {
    owner_boundary(world);
    const auto* slot = storage(world);
    if (!slot || !slot->runtime)
        return false;
    const auto lease = slot->runtime;
    auto& runtime = *lease;
    require(!runtime.advancing, "Application state updates cannot nest");
    if (runtime.initialized && !pending_override &&
        std::none_of(runtime.domains.begin(), runtime.domains.end(), [&](const auto& domain) {
            return world.entity(domain.entity)
                .template has<ApplicationStateRequested>(flecs::Wildcard);
        }))
        return false;
    const auto retirement_handler = slot->retired;
    struct Guard {
        bool& value;
        explicit Guard(bool& v) : value(v) { value = true; }
        ~Guard() { value = false; }
    } guard(runtime.advancing);
    std::vector<std::size_t> previous, next(runtime.domains.size(), none), pending;
    for (const auto& domain : runtime.domains) {
        const auto entity = world.entity(domain.entity);
        previous.push_back(leaf(domain, entity.target<ApplicationStateActive>()));
        pending.push_back(leaf(domain, entity.target<ApplicationStateRequested>()));
    }
    if (pending_override)
        pending[pending_override->domain] = pending_override->state;
    for (const auto i : runtime.order) {
        const auto& domain = runtime.domains[i];
        const auto& definition = domain.definition;
        if (definition.enabled_when && !matches(runtime, next, *definition.enabled_when))
            continue;
        auto state = previous[i] == none ? domain.indices.at(definition.initial) : previous[i];
        if (definition.computed) {
            state = domain.indices.at(definition.initial);
            for (const auto& rule : definition.rules)
                if (std::all_of(rule.all.begin(), rule.all.end(), [&](const auto& condition) {
                        return matches(runtime, next, condition);
                    })) {
                    state = domain.indices.at(rule.state);
                    break;
                }
        } else if (pending[i] != none && (previous[i] != none || !runtime.initialized))
            state = pending[i];
        next[i] = state;
    }
    for (std::size_t i = 0; i < runtime.domains.size(); ++i)
        require(next[i] == previous[i] ||
                    runtime.domains[i].revision != std::numeric_limits<std::uint64_t>::max(),
                "Application state revision exhausted");
    // Consume the captured requests before callbacks can submit fresh requests.
    for (std::size_t i = 0; i < runtime.domains.size(); ++i)
        if (pending[i] != none)
            world.entity(runtime.domains[i].entity)
                .remove<ApplicationStateRequested>(flecs::Wildcard);
    bool changed = false;
    for (std::size_t i = 0; i < runtime.domains.size(); ++i) {
        if (next[i] == previous[i])
            continue;
        changed = true;
        auto& domain = runtime.domains[i];
        publish_active(world, domain, next[i]);
        ++domain.revision;
    }
    runtime.initialized = true;
    // Snapshot all retiring roots before native destruction invokes observers.
    // The same Scene/Flecs structural cleanup handles children and Prefab groups.
    std::vector<flecs::entity_t> retiring;
    for (std::size_t i = 0; i < runtime.domains.size(); ++i) {
        if (next[i] == previous[i])
            continue;
        const auto& domain = runtime.domains[i];
        auto old_path = path(domain, previous[i]), new_path = path(domain, next[i]);
        while (!old_path.empty() && !new_path.empty() && old_path.back() == new_path.back()) {
            old_path.pop_back();
            new_path.pop_back();
        }
        for (const auto state : old_path) {
            auto query = world.query_builder()
                             .with<ApplicationStateLifetime>(domain.states[state])
                             .query_flags(EcsQueryMatchDisabled)
                             .build();
            query.each([&](flecs::entity entity) { retiring.push_back(entity.id()); });
        }
    }
    std::sort(retiring.begin(), retiring.end());
    retiring.erase(std::unique(retiring.begin(), retiring.end()), retiring.end());
    for (const auto root : retiring)
        if (world.is_alive(root))
            world.entity(root).destruct();
    if (changed && retirement_handler)
        retirement_handler();
    for (std::size_t i = 0; i < runtime.domains.size(); ++i) {
        if (next[i] == previous[i])
            continue;
        const auto& domain = runtime.domains[i];
        auto old_path = path(domain, previous[i]), new_path = path(domain, next[i]);
        while (!old_path.empty() && !new_path.empty() && old_path.back() == new_path.back()) {
            old_path.pop_back();
            new_path.pop_back();
        }
        const auto old_key = previous[i] == none ? "" : domain.definition.states[previous[i]].key;
        const auto new_key = next[i] == none ? "" : domain.definition.states[next[i]].key;
        for (const auto state : old_path) {
            ApplicationStateExited event{domain.definition.key, domain.definition.states[state].key,
                                         old_key, new_key, domain.revision};
            world.event<ApplicationStateExited>()
                .id<ApplicationStateDomainTag>()
                .entity(domain.entity)
                .ctx(event)
                .emit();
        }
        for (auto state = new_path.rbegin(); state != new_path.rend(); ++state) {
            ApplicationStateEntered event{domain.definition.key,
                                          domain.definition.states[*state].key, old_key, new_key,
                                          domain.revision};
            world.event<ApplicationStateEntered>()
                .id<ApplicationStateDomainTag>()
                .entity(domain.entity)
                .ctx(event)
                .emit();
        }
    }
    return changed;
}
} // namespace
bool advance_application_states(flecs::world& world) {
    return advance_application_states_impl(world, {});
}
bool initialize_application_states(flecs::world& world) {
    owner_boundary(world);
    const auto* slot = storage(world);
    return slot && slot->runtime && !slot->runtime->initialized &&
           advance_application_states(world);
}
nlohmann::json checkpoint_application_states(const flecs::world& world) {
    owner_boundary(world);
    const auto* slot = storage(world);
    if (!slot || !slot->runtime)
        return nullptr;
    const auto& runtime = *slot->runtime;
    require(runtime.initialized && !runtime.advancing,
            "Application state checkpoint requires an initialized boundary");
    auto rows = nlohmann::json::array();
    for (const auto& view : observe_application_states(world))
        rows.push_back({{"domain", view.domain},
                        {"active", view.active},
                        {"requested", view.requested},
                        {"revision", view.revision}});
    return {{"version", 1},
            {"definition_digest", runtime.definition_digest},
            {"domains", std::move(rows)}};
}
void restore_application_states(flecs::world& world, const nlohmann::json& value) {
    owner_boundary(world);
    const auto* slot = storage(world);
    if (!slot || !slot->runtime) {
        require(value.is_null(),
                "Application state recovery has no matching configured definition");
        return;
    }
    auto& runtime = *slot->runtime;
    require(!runtime.initialized && !runtime.advancing,
            "Application state recovery must precede initialization");
    const auto text = value.dump();
    require(
        asset_detail::parse_bounded_json(std::as_bytes(std::span(text)), 16384, 3000, 6) == value &&
            value.is_object() && value.size() == 3 && value.at("version").is_number_integer() &&
            value.at("version") == 1 &&
            value.at("definition_digest") == runtime.definition_digest &&
            value.at("domains").is_array() && value.at("domains").size() == runtime.domains.size(),
        "Application state recovery definition/version/bounds mismatch");
    std::vector<std::size_t> active(runtime.domains.size(), none), pending(active);
    std::vector<std::uint64_t> revisions(runtime.domains.size());
    std::vector<bool> seen(runtime.domains.size());
    for (const auto& row : value.at("domains")) {
        require(row.is_object() && row.size() == 4 && row.at("revision").is_number_unsigned(),
                "Invalid application state recovery row");
        const auto at = runtime.indices.find(row.at("domain").get<std::string>());
        require(at != runtime.indices.end() && !seen[at->second],
                "Unknown or duplicate application state recovery domain");
        const auto i = at->second;
        seen[i] = true;
        const auto& domain = runtime.domains[i];
        const auto state = [&](const char* field) {
            const auto key = row.at(field).get<std::string>();
            if (key.empty())
                return none;
            const auto found = domain.indices.find(key);
            require(found != domain.indices.end(), "Unknown application state recovery key");
            return found->second;
        };
        active[i] = state("active");
        pending[i] = state("requested");
        revisions[i] = row.at("revision").get<std::uint64_t>();
        require((pending[i] == none || (!domain.definition.computed && active[i] != none)) &&
                    (active[i] == none || revisions[i] != 0),
                "Invalid application state recovery pending request/revision");
    }
    for (const auto i : runtime.order) {
        const auto& domain = runtime.domains[i];
        const auto& definition = domain.definition;
        const bool enabled =
            !definition.enabled_when || matches(runtime, active, *definition.enabled_when);
        require(enabled == (active[i] != none),
                "Application state recovery subordinate condition mismatch");
        if (enabled && definition.computed) {
            auto selected = domain.indices.at(definition.initial);
            for (const auto& rule : definition.rules)
                if (std::all_of(rule.all.begin(), rule.all.end(),
                                [&](const auto& c) { return matches(runtime, active, c); })) {
                    selected = domain.indices.at(rule.state);
                    break;
                }
            require(selected == active[i], "Application state recovery computed value mismatch");
        }
    }
    // All semantic validation precedes native mutation. No lifecycle actions replay.
    runtime.advancing = true;
    for (std::size_t i = 0; i < runtime.domains.size(); ++i) {
        auto& domain = runtime.domains[i];
        publish_active(world, domain, active[i]);
        auto entity = world.entity(domain.entity);
        entity.remove<ApplicationStateRequested>(flecs::Wildcard);
        if (pending[i] != none)
            entity.add<ApplicationStateRequested>(domain.states[pending[i]]);
        domain.revision = revisions[i];
    }
    runtime.initialized = true;
    runtime.advancing = false;
}
std::vector<ApplicationStateObservation> observe_application_states(const flecs::world& world) {
    std::vector<ApplicationStateObservation> result;
    const auto* slot = storage(world);
    if (!slot || !slot->runtime)
        return result;
    for (const auto& domain : slot->runtime->domains) {
        const auto entity = world.entity(domain.entity);
        ApplicationStateObservation view;
        view.domain = domain.definition.key;
        view.label = domain.definition.label;
        view.computed = domain.definition.computed;
        view.revision = domain.revision;
        const auto state = leaf(domain, entity.target<ApplicationStateActive>());
        if (state != none)
            view.active = domain.definition.states[state].key;
        const auto requested = leaf(domain, entity.target<ApplicationStateRequested>());
        if (requested != none)
            view.requested = domain.definition.states[requested].key;
        for (const auto member : path(domain, state))
            view.membership.push_back(domain.definition.states[member].key);
        result.push_back(std::move(view));
    }
    return result;
}
} // namespace forge
