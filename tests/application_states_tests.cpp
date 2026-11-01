#include <algorithm>
#include <forge/application_states.hpp>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace {
using namespace forge;
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F&& f) {
    bool failed = false;
    try {
        f();
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed, "Invalid state operation was accepted");
}
ApplicationStatesDefinition fixture() {
    ApplicationStateDomainDefinition app;
    app.key = "app";
    app.label = "Game mode";
    app.initial = "menu";
    app.states = {{"menu", "Menu", ""},
                  {"game", "Game", ""},
                  {"playing", "Playing", "game"},
                  {"paused", "Paused", "game"}};
    ApplicationStateDomainDefinition level;
    level.key = "level";
    level.initial = "warmup";
    level.states = {{"warmup", "Warmup", ""}, {"round", "Round", ""}};
    level.enabled_when = {"app", "game"};
    ApplicationStateDomainDefinition hud;
    hud.key = "hud";
    hud.initial = "hidden";
    hud.states = {{"hidden", "Hidden", ""}, {"visible", "Visible", ""}, {"ready", "Ready", ""}};
    hud.computed = true;
    hud.rules = {{"ready", {{"app", "playing"}, {"level", "round"}}},
                 {"visible", {{"app", "game"}}}};
    // Deliberately place dependencies after their consumers.
    return {{hud, level, app}};
}
const ApplicationStateObservation& find(const std::vector<ApplicationStateObservation>& values,
                                        const std::string& domain) {
    for (const auto& value : values)
        if (value.domain == domain)
            return value;
    throw std::runtime_error("Missing domain observation");
}
std::string active(const flecs::world& world, const std::string& domain) {
    const auto values = observe_application_states(world);
    return find(values, domain).active;
}
void transitions_and_queries() {
    flecs::world world;
    register_application_states(world);
    const auto domain = application_state_domain(world, "app");
    const auto playing = application_state_entity(world, "app", "playing");
    const auto game = application_state_entity(world, "app", "game");
    struct Counter {
        int ticks = 0;
    };
    const auto actor = world.entity().set<Counter>({});
    auto conditional =
        world.query_builder<Counter>().with<ApplicationStateMember>(game).src(domain).build();
    check(conditional.count() == 0, "Unconfigured state matched");
    configure_application_states(world, fixture());
    check(conditional.count() == 0 && active(world, "app").empty(),
          "Configuration advanced state outside its boundary");
    std::vector<std::string> events;
    auto entered =
        world.observer().event<ApplicationStateEntered>().with<ApplicationStateDomainTag>().run(
            [&](flecs::iter& it) {
                while (it.next()) {
                    const auto& event = *it.param<ApplicationStateEntered>();
                    events.push_back("+" + event.domain + ":" + event.state);
                    if (event.domain == "app" && event.state == "playing") {
                        check(active(world, "level") == "warmup" &&
                                  active(world, "hud") == "visible",
                              "Observer saw a partially published configuration");
                    }
                }
            });
    auto exited =
        world.observer().event<ApplicationStateExited>().with<ApplicationStateDomainTag>().run(
            [&](flecs::iter& it) {
                while (it.next()) {
                    const auto& event = *it.param<ApplicationStateExited>();
                    events.push_back("-" + event.domain + ":" + event.state);
                }
            });
    check(advance_application_states(world), "Initial entry missing");
    check(events == std::vector<std::string>({"+hud:hidden", "+app:menu"}), "Initial events");
    events.clear();
    rejects([&] { request_application_state(world, "level", "round"); });
    rejects([&] { request_application_state(world, "hud", "visible"); });
    request_application_state(world, "app", "playing");
    check(active(world, "app") == "menu", "Request changed state immediately");
    check(advance_application_states(world), "Playing entry missing");
    check(domain.target<ApplicationStateActive>() == playing &&
              domain.has<ApplicationStateMember>(playing) &&
              domain.has<ApplicationStateMember>(game),
          "Native authority/membership mismatch");
    conditional.each([](Counter& value) { ++value.ticks; });
    check(actor.get<Counter>().ticks == 1, "Native conditional query did not run");
    check(events == std::vector<std::string>({"-hud:hidden", "+hud:visible", "+level:warmup",
                                              "-app:menu", "+app:game", "+app:playing"}),
          "Cross-domain/hierarchy event order");
    request_application_state(world, "level", "round");
    advance_application_states(world);
    check(active(world, "hud") == "ready", "Computed first-match/all conditions failed");
    events.clear();
    request_application_state(world, "app", "paused");
    advance_application_states(world);
    check(active(world, "level") == "round" && active(world, "hud") == "visible",
          "Ancestor gate did not preserve subordinate value");
    check(std::find(events.begin(), events.end(), "-app:game") == events.end() &&
              events[events.size() - 2] == "-app:playing" && events.back() == "+app:paused",
          "Shared ancestor was unnecessarily exited");
    request_application_state(world, "app", "menu");
    request_application_state(world, "level", "warmup");
    advance_application_states(world);
    check(active(world, "level").empty() && !domain.has<ApplicationStateMember>(game),
          "Subordinate deactivation failed");
    check(conditional.count() == 0, "Native condition remained active");
    entered.destruct();
    request_application_state(world, "app", "playing");
    advance_application_states(world);
    check(active(world, "level") == "warmup", "Subordinate re-entry did not reset initial state");
    const auto before = observe_application_states(world);
    request_application_state(world, "app", "playing");
    check(!advance_application_states(world), "Same state emitted a transition");
    check(find(observe_application_states(world), "app").revision == find(before, "app").revision,
          "Same state advanced revision");
    exited.destruct();
}
void deferred_and_reentrant() {
    flecs::world world;
    configure_application_states(world, fixture());
    advance_application_states(world);
    world.defer_begin();
    request_application_state(world, "app", "playing");
    request_application_state(world, "app", "paused");
    rejects([&] { advance_application_states(world); });
    check(find(observe_application_states(world), "app").requested.empty(),
          "Deferred request leaked");
    world.defer_end();
    check(find(observe_application_states(world), "app").requested == "paused", "Last merge lost");
    auto observer =
        world.observer().event<ApplicationStateEntered>().with<ApplicationStateDomainTag>().run(
            [&](flecs::iter& it) {
                while (it.next()) {
                    const auto& event = *it.param<ApplicationStateEntered>();
                    if (event.domain == "app" && event.state == "paused") {
                        rejects([&] { advance_application_states(world); });
                        rejects([&] { configure_application_states(world, fixture()); });
                        request_application_state(world, "app", "menu");
                    }
                }
            });
    advance_application_states(world);
    check(active(world, "app") == "paused" &&
              find(observe_application_states(world), "app").requested == "menu",
          "Observer request was lost or applied recursively");
    advance_application_states(world);
    check(active(world, "app") == "menu", "Observer request did not apply at next boundary");
    // Query/system execution uses the actual native stage, not a separate request queue.
    auto system = world.system<>().run([&](flecs::iter& it) {
        auto stage = it.world();
        request_application_state(stage, "app", "playing");
    });
    world.progress(.01f);
    check(active(world, "app") == "menu", "Gameplay request bypassed boundary");
    advance_application_states(world);
    check(active(world, "app") == "playing", "Staged request was not merged");
    system.destruct();
    observer.destruct();
    unsigned exits = 0;
    world.observer().event<ApplicationStateExited>().with<ApplicationStateDomainTag>().run(
        [&](flecs::iter& it) {
            while (it.next())
                ++exits;
        });
    // Destruction notifications are checked separately with observer storage alive.
    check(exits == 0, "Unexpected exit event");
}
void validation_and_independence() {
    auto value = fixture();
    value.validate();
    rejects([&] {
        auto v = value;
        v.domains[2].states[1].parent = "playing";
        v.validate();
    });
    rejects([&] {
        auto v = value;
        v.domains[2].states[1].parent = "absent";
        v.validate();
    });
    rejects([&] {
        auto v = value;
        v.domains[2].initial = "absent";
        v.validate();
    });
    rejects([&] {
        auto v = value;
        v.domains[2].key = "../bad";
        v.validate();
    });
    rejects([&] {
        auto v = value;
        v.domains.push_back(v.domains.back());
        v.validate();
    });
    rejects([&] {
        auto v = value;
        v.domains[0].rules[0].all[0].state = "absent";
        v.validate();
    });
    rejects([&] {
        auto v = value;
        v.domains[2].enabled_when = {"hud", "ready"};
        v.validate();
    });
    rejects([&] {
        auto v = value;
        v.domains[1].rules = value.domains[0].rules;
        v.validate();
    });
    rejects([&] {
        auto v = value;
        v.domains[0].rules[0].all.clear();
        v.validate();
    });
    flecs::world a, b;
    auto invalid = value;
    invalid.domains[0].initial = "absent";
    rejects([&] { configure_application_states(a, invalid); });
    check(observe_application_states(a).empty(), "Failed admission installed state");
    configure_application_states(a, value);
    configure_application_states(b, value);
    advance_application_states(a);
    advance_application_states(b);
    request_application_state(a, "app", "playing");
    advance_application_states(a);
    check(active(b, "app") == "menu", "Independent worlds shared active state");
    const auto copied = observe_application_states(a);
    request_application_state(a, "app", "menu");
    advance_application_states(a);
    check(find(copied, "app").active == "playing", "Observation borrowed mutable state");
    rejects([&] { request_application_state(a, "absent", "menu"); });
    rejects([&] { request_application_state(a, "app", "absent"); });
    rejects([&] { configure_application_states(a, value); });
    unsigned exits = 0;
    {
        flecs::world retiring;
        configure_application_states(retiring, value);
        advance_application_states(retiring);
        retiring.observer().event<ApplicationStateExited>().with<ApplicationStateDomainTag>().run(
            [&](flecs::iter& it) {
                while (it.next())
                    ++exits;
            });
    }
    check(exits == 0, "World retirement ran logical exit actions");
}
void recovery() {
    using Json = nlohmann::json;
    const auto asset = AssetId::generate();
    const auto definition = fixture();
    flecs::world original;
    configure_application_states(original, definition, asset);
    rejects([&] { checkpoint_application_states(original); });
    check(initialize_application_states(original), "Startup did not initialize state");
    request_application_state(original, "app", "playing");
    advance_application_states(original);
    request_application_state(original, "level", "round");
    advance_application_states(original);
    request_application_state(original, "app", "paused");
    const auto checkpoint = checkpoint_application_states(original);
    const auto frozen = observe_application_states(original);
    const auto row = [](Json& value, const std::string& domain) -> Json& {
        for (auto& item : value.at("domains"))
            if (item.at("domain") == domain)
                return item;
        throw std::runtime_error("Missing checkpoint domain");
    };
    const auto invalid = [&](const auto& mutate) {
        auto bad = checkpoint;
        mutate(bad);
        flecs::world candidate;
        configure_application_states(candidate, definition, asset);
        rejects([&] { restore_application_states(candidate, bad); });
        for (const auto& view : observe_application_states(candidate))
            check(view.active.empty() && view.requested.empty() && !view.revision,
                  "Invalid recovery partially changed native state");
        restore_application_states(candidate, checkpoint);
        check(active(candidate, "hud") == "ready", "Invalid restore poisoned a valid retry");
    };
    invalid([](Json& value) { value["definition_digest"] = std::string(64, '0'); });
    invalid([](Json& value) { value["version"] = 1.0; });
    invalid([](Json& value) { value["domains"].erase(0); });
    invalid([](Json& value) { value["domains"][0] = value["domains"][1]; });
    invalid([&](Json& value) { row(value, "app")["active"] = "native_handle_1234"; });
    invalid([&](Json& value) { row(value, "app")["active"] = ""; });
    invalid([&](Json& value) { row(value, "app")["revision"] = std::uint64_t{0}; });
    invalid([&](Json& value) { row(value, "app")["revision"] = -1; });
    invalid([&](Json& value) { row(value, "hud")["requested"] = "visible"; });
    invalid([&](Json& value) { row(value, "hud")["active"] = "hidden"; });
    invalid([&](Json& value) { row(value, "level")["active"] = ""; });
    flecs::world restored;
    for (unsigned i = 0; i < 200; ++i)
        restored.entity();
    configure_application_states(restored, definition, asset);
    unsigned events = 0;
    restored.observer().event<ApplicationStateEntered>().with<ApplicationStateDomainTag>().run(
        [&](flecs::iter& it) {
            while (it.next())
                ++events;
        });
    restored.observer().event<ApplicationStateExited>().with<ApplicationStateDomainTag>().run(
        [&](flecs::iter& it) {
            while (it.next())
                ++events;
        });
    restore_application_states(restored, checkpoint);
    check(!events && checkpoint_application_states(restored) == checkpoint,
          "Recovery replayed lifecycle actions or changed semantic state");
    check(!initialize_application_states(restored) && !events &&
              checkpoint_application_states(restored) == checkpoint,
          "Repeated initialization consumed restored pending requests");
    const auto copied = observe_application_states(restored);
    for (std::size_t i = 0; i < copied.size(); ++i)
        check(copied[i].active == frozen[i].active &&
                  copied[i].membership == frozen[i].membership &&
                  copied[i].revision == frozen[i].revision &&
                  copied[i].requested == frozen[i].requested,
              "Recovery depended on native IDs or lost revision/membership");
    advance_application_states(restored);
    check(active(restored, "app") == "paused" && active(restored, "hud") == "visible" &&
              active(restored, "level") == "round" && events > 0,
          "Recovered pending request did not resume at the next boundary");
    rejects([&] { restore_application_states(restored, checkpoint); });
    flecs::world wrong_asset;
    configure_application_states(wrong_asset, definition, AssetId::generate());
    rejects([&] { restore_application_states(wrong_asset, checkpoint); });
    flecs::world no_definition;
    check(checkpoint_application_states(no_definition).is_null(),
          "Empty World made a state checkpoint");
    restore_application_states(no_definition, Json());
    rejects([&] { restore_application_states(no_definition, checkpoint); });
}
void host_controls_and_sdk_queries() {
    using Json = nlohmann::json;
    flecs::world world;
    check(application_state_current(world, "app").empty() &&
              !application_state_is(world, "app", "playing") &&
              !world.id_if_registered<ApplicationStateActive>() &&
              !world.id_if_registered<ApplicationStateMember>(),
          "Read-only SDK helpers created native registrations");
    configure_application_states(world, fixture());
    const auto before = application_states_status(world);
    check(!before.at("initialized").get<bool>(), "Status initialized the definition");
    rejects([&] { control_application_states(world, {{"domain", "app"}, {"state", "playing"}}); });
    initialize_application_states(world);
    const auto status = application_states_status(world, true);
    check(status.at("domains").size() == 3 && status.at("domains")[0].at("states").size() == 3,
          "Host discovery omitted immutable domain choices");
    control_application_states(world, {{"operation", "application_state"},
                                       {"domain", "app"},
                                       {"state", "playing"},
                                       {"expected_revision", std::uint64_t{1}}});
    check(application_state_current(world, "app") == "playing" &&
              application_state_is(world, "app", "game") &&
              !application_state_is(world, "app", "menu"),
          "SDK helpers disagree with native active/ancestor relationships");
    const auto committed = application_states_status(world);
    check(!committed.at("domains")[0].contains("states"),
          "Routine status copied unnecessary choices");
    for (const auto& bad : std::vector<Json>{
             {{"domain", "app"}, {"state", "menu"}, {"expected_revision", std::uint64_t{1}}},
             {{"domain", "app"}, {"state", "menu"}, {"expected_revision", 2.0}},
             {{"domain", "app"}, {"state", "menu"}, {"unexpected", true}},
             {{"operation", "other"}, {"domain", "app"}, {"state", "menu"}},
             {{"domain", "hud"}, {"state", "visible"}},
             {{"domain", "app"}, {"state", "missing"}}}) {
        rejects([&] { control_application_states(world, bad); });
        check(application_states_status(world) == committed,
              "Rejected host request changed pending/active state or revision");
    }
    auto altered = application_states_status(world, true);
    altered["domains"][0]["states"][0]["label"] = "External copy";
    check(application_states_status(world, true) != altered,
          "Host status borrowed mutable definition data");
    auto observer =
        world.observer().event<ApplicationStateEntered>().with<ApplicationStateDomainTag>().run(
            [&](flecs::iter& it) {
                while (it.next()) {
                    const auto& event = *it.param<ApplicationStateEntered>();
                    if (event.domain == "app" && event.state == "paused") {
                        const auto snapshot = application_states_status(world);
                        rejects([&] {
                            control_application_states(world,
                                                       {{"domain", "app"}, {"state", "menu"}});
                        });
                        check(application_states_status(world) == snapshot,
                              "Recursive host request leaked a pending change");
                    }
                }
            });
    control_application_states(world, {{"domain", "app"}, {"state", "paused"}});
    observer.destruct();
}
void exhausted_control_preserves_pending() {
    // Both the requested domain and a derived domain can prevent publication.
    // A rejected host control must preserve previously queued native intent.
    for (const auto* exhausted : {"app", "hud"}) {
        flecs::world initial;
        configure_application_states(initial, fixture());
        initialize_application_states(initial);
        auto checkpoint = checkpoint_application_states(initial);
        for (auto& row : checkpoint["domains"]) {
            if (row.at("domain") == exhausted)
                row["revision"] = std::numeric_limits<std::uint64_t>::max();
            if (row.at("domain") == "app")
                row["requested"] = "game";
        }
        flecs::world restored;
        configure_application_states(restored, fixture());
        restore_application_states(restored, checkpoint);
        const auto before = checkpoint_application_states(restored);
        rejects([&] {
            control_application_states(restored, {{"domain", "app"}, {"state", "playing"}});
        });
        check(checkpoint_application_states(restored) == before,
              "Counter-exhausted host control changed active/revision/pending state");
    }
}
} // namespace
int main() {
    try {
        transitions_and_queries();
        deferred_and_reentrant();
        validation_and_independence();
        recovery();
        host_controls_and_sdk_queries();
        exhausted_control_preserves_pending();
        std::cout
            << "Application states: transitions, hierarchy, computed/subordinate domains, native "
               "conditions, staged requests, lifecycle and isolation passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
