#include "authored_schema.hpp"
#include <cmath>
#include <forge/application_state_components.hpp>
#include <forge/application_states.hpp>
#include <forge/game_session.hpp>
#include <forge/input_message.hpp>
#include <forge/runtime.hpp>
#include <iostream>

namespace {
using namespace forge;
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F&& operation) {
    try {
        operation();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid Input Message was accepted");
}
const std::string action = "23000000-0000-4230-8230-000000000001";
InputMap map() {
    return InputMap(
        {{"version", 1},
         {"actions", Json::array({{{"id", action},
                                   {"name", "Send"},
                                   {"kind", "digital"},
                                   {"bindings", Json::array({{{"control", "key.space"}}})}}})}});
}
struct Payload {
    std::string title;
    std::uint64_t number = 0;
};
EngineModule native_module() {
    EngineModule module;
    module.id = "test.messages";
    module.dependencies = {"forge.core"};
    module.schemas = [](ModuleContext& context) {
        auto type = context.world.component<Payload>("project.MessageValue")
                        .member<std::string>("title")
                        .member<std::uint64_t>("number")
                        .add(flecs::OnInstantiate, flecs::Inherit);
        detail::opt_in_authoring(context.world, type, "project.message_value", "test.messages", 1,
                                 {{"title", "Ready"}, {"number", 0}}, "Gameplay");
    };
    return module;
}
void authoring() {
    WorldContext context;
    Scene scene(context);
    scene.replace(
        {{"version", 1},
         {"entities",
          Json::array({{{"id", "source"}, {"name", "Source"}, {"components", Json::object()}}})}});
    const auto original = scene.document();
    auto edited = original;
    edited["entities"][0]["components"]["forge.input_message"] = {
        {"enabled", true}, {"action", action}, {"payload", "forge.tint"}};
    scene.edit(edited);
    check(scene.effective_document()["entities"][0]["components"].contains("forge.input_message"),
          "Input Message did not become an authored native component");
    scene.undo();
    check(scene.document() == original, "Undo changed unrelated source");
    scene.redo();
    check(scene.document() == edited, "Redo did not restore authored emitter");
    for (const auto* field : {"action", "payload"}) {
        auto bad = edited;
        bad["entities"][0]["components"]["forge.input_message"][field] = "invalid key!";
        rejects([&] { scene.edit(bad); });
        check(scene.document() == edited, "Rejected emitter changed source/history");
    }
    bool found = false;
    for (const auto& type : context.schema().at("components"))
        if (type.at("id") == "forge.input_message") {
            found = true;
            check(type.at("optional") == true && type.at("category") == "Gameplay",
                  "Input Message schema is not discoverable");
        }
    check(found, "Input Message metadata missing");
}
void dispatch() {
    EngineContext engine(WorldRole::Runtime, false, {native_module()});
    auto& context = engine.world();
    auto& world = context.world();
    Scene scene(context);
    scene.replace(
        {{"version", 1},
         {"entities",
          Json::array({{{"id", "source"}, {"name", "Source"}, {"components", Json::object()}}})}});
    const auto source = scene.entity("source");
    source.set<InputMessage>({true, action, "project.message_value"});
    auto base = world.prefab().set<Payload>({"snapshot", UINT64_MAX});
    source.is_a(base);
    int calls = 0, fixed = 0;
    bool observer_valid = true, ordered = true;
    world.observer().event<Payload>().with(flecs::Any).each([&](flecs::iter& it, std::size_t row) {
        const auto* value = it.param<Payload>();
        observer_valid &= value && value->title == "snapshot" && value->number == UINT64_MAX;
        observer_valid &= it.entity(row) == source;
        ++calls;
        source.set<Payload>({"changed", 7});
        observer_valid &= value && value->title == "snapshot";
    });
    world.system("test.message_order")
        .kind(world.entity("forge.runtime.Gameplay"))
        .run([&](flecs::iter&) {
            ordered &= calls == 1;
            ++fixed;
        })
        .add<FixedSimulation>();
    RuntimeSimulation runtime(context, scene);
    runtime.input().configure(map());
    const auto source_document = scene.document();
    runtime.input().submit({{"key.space", 1}});
    runtime.tick(1.f / 60);
    check(observer_valid && ordered, "Observer payload or fixed dispatch order was incorrect");
    check(calls == 1 && fixed == 1, "Pressed edge did not dispatch");
    check(runtime.input_status()["messages"]["emitted"] == 1, "Input status omitted delivery");
    runtime.tick(1.f / 60);
    check(calls == 1 && fixed == 2, "Held action repeated a message");
    check(runtime.input_status()["messages"]["emitted_total"] == 1 &&
              runtime.input_status()["messages"]["last_sent_tick"] == 1,
          "Message counters lost short dispatch between observations");
    check(runtime.input_status()["messages"]["emitted"] == 0, "Tick status retained old delivery");
    check(scene.document()["entities"][0]["components"]["forge.input_message"] ==
              source_document["entities"][0]["components"]["forge.input_message"],
          "Runtime messages rewrote emitter configuration");
    source.set<InputMessage>({true, action, "project.unavailable"});
    runtime.tick(1.f / 60);
    check(runtime.input_status()["messages"]["sources"][0]["state"] == "Error",
          "Missing payload did not produce a diagnostic");
    source.set<InputMessage>({false, action, "project.unavailable"});
    runtime.tick(1.f / 60);
    check(runtime.input_status()["messages"]["sources"][0]["state"] == "Inactive",
          "Disabled emitter still resolved payload");
}
void session_lifecycle() {
    WorldContext authoring;
    Scene scene(authoring);
    scene.replace(
        {{"version", 1},
         {"entities",
          Json::array(
              {{{"id", "source"},
                {"name", "Source"},
                {"components",
                 {{"forge.tint", {{"r", .2}, {"g", .3}, {"b", .4}}},
                  {"forge.input_message",
                   {{"enabled", true}, {"action", action}, {"payload", "forge.tint"}}}}}}})}});
    const auto authored = scene.document(), snapshot = scene.snapshot();
    int calls = 0;
    bool value_ok = true;
    EngineModule observer;
    observer.id = "test.message_receiver";
    observer.dependencies = {"forge.core"};
    observer.runtime_roles = role_mask(WorldRole::Runtime);
    observer.start = [&](ModuleContext& context) {
        context.world.observer()
            .event<Tint>()
            .with(flecs::Any)
            .each([&](flecs::iter& it, std::size_t) {
                const auto* value = it.param<Tint>();
                value_ok &= value && std::abs(value->g - .3f) < 1e-6;
                ++calls;
            });
    };
    GameSessionConfig config;
    config.input = map();
    config.modules = {observer};
    GameSession game(config);
    auto ticket = game.prepare(snapshot);
    const auto now = RuntimeClock::Clock::now();
    game.activate(ticket, now, false);
    game.input({{"key.space", 1}});
    game.control_frame();
    game.sample_time(now + std::chrono::seconds(1));
    check(calls == 0, "Pause or control-only frame sent a message");
    game.step();
    check(calls == 1 && value_ok, "Step lost queued action or built-in payload");
    game.step();
    check(calls == 1, "Step repeated a held message");
    game.input({{"key.space", 0}, {"key.space", 1}});
    game.step();
    check(calls == 2, "Short release/press between ticks lost its message");
    ticket = game.prepare(snapshot);
    game.activate(ticket, now, false);
    game.step();
    check(calls == 2, "Replacement runtime replayed a retired message");
    game.input({{"key.space", 1}});
    game.step();
    check(calls == 3 && scene.document() == authored && scene.snapshot() == snapshot,
          "Restart lost delivery or mutated the separate authored scene");
}
void bounded_mutations() {
    EngineContext engine(WorldRole::Runtime);
    Scene scene(engine.world());
    Json entities = Json::array();
    for (unsigned i = 0; i != 257; ++i)
        entities.push_back(
            {{"id", "source" + std::to_string(i)},
             {"name", "Source"},
             {"components",
              {{"forge.tint", {{"r", .2}, {"g", .3}, {"b", .4}}},
               {"forge.input_message",
                {{"enabled", true}, {"action", action}, {"payload", "forge.tint"}}}}}});
    scene.replace({{"version", 1}, {"entities", entities}});
    RuntimeSimulation runtime(engine.world(), scene);
    runtime.input().configure(map());
    auto& world = scene.world();
    int first = 0, second = 0;
    bool snapshots = true, deferred = true;
    const auto removed = scene.entity("source0");
    world.observer().event<Tint>().with(flecs::Any).each([&](flecs::iter& it, std::size_t row) {
        const auto* value = it.param<Tint>();
        snapshots &= value && std::abs(value->r - .2f) < 1e-6;
        ++first;
        if (first == 1)
            removed.destruct();
        deferred &= world.is_alive(removed);
        it.entity(row).set<Tint>({.9f, .8f, .7f});
    });
    world.observer().event<Tint>().with(flecs::Any).each([&](flecs::iter& it, std::size_t) {
        const auto* value = it.param<Tint>();
        snapshots &= value && std::abs(value->r - .2f) < 1e-6;
        ++second;
    });
    runtime.input().submit({{"key.space", 1}});
    runtime.tick(1.f / 60);
    const auto status = runtime.input_status()["messages"];
    check(first == 256 && second == 256 && snapshots && deferred && !world.is_alive(removed),
          "Bounded snapshot batch or deferred mutation boundary was incorrect");
    check(status["emitted"] == 256 && status["overflow"] == 1 && status["sources"].size() == 256,
          "Emitter bound did not report omitted sources");
}
void aggregate_values() {
    EngineContext engine(WorldRole::Runtime, false, {native_module()});
    Scene scene(engine.world());
    Json entities = Json::array();
    for (unsigned i = 0; i != 80; ++i)
        entities.push_back({{"id", "source" + std::to_string(i)},
                            {"name", "Source"},
                            {"components", Json::object()}});
    scene.replace({{"version", 1}, {"entities", entities}});
    for (unsigned i = 0; i != 80; ++i)
        scene.entity("source" + std::to_string(i))
            .set<Payload>({std::string(60000, 'v'), UINT64_MAX})
            .set<InputMessage>({true, action, "project.message_value"});
    int received = 0;
    bool correct = true;
    scene.world()
        .observer()
        .event<Payload>()
        .with(flecs::Any)
        .each([&](flecs::iter& it, std::size_t) {
            const auto* value = it.param<Payload>();
            correct &=
                value && value->title == std::string(60000, 'v') && value->number == UINT64_MAX;
            ++received;
        });
    RuntimeSimulation runtime(engine.world(), scene);
    runtime.input().configure(map());
    runtime.input().submit({{"key.space", 1}});
    runtime.tick(1.f / 60);
    const auto status = runtime.input_status()["messages"];
    unsigned errors = 0;
    for (const auto& source : status.at("sources"))
        if (source.at("state") == "Error") {
            ++errors;
            check(source.at("diagnostic") == "Input Message tick payload budget exceeds 4 MiB",
                  "Aggregate-budget rejection concealed another failure");
        }
    check(received == 69 && errors == 11 && correct && status.at("emitted") == 69,
          "Aggregate native string snapshot bound or cleanup count is incorrect");
}
void invalid_routes_and_conditions() {
    EngineContext engine(WorldRole::Runtime, false, {native_module()});
    auto& context = engine.world();
    Scene scene(context);
    scene.replace(
        {{"version", 1},
         {"entities",
          Json::array({{{"id", "source"}, {"name", "Source"}, {"components", Json::object()}}})}});
    const auto source = scene.entity("source");
    source.set<Payload>({"value", 0});
    source.set<InputMessage>({true, action, "project.message_value"});
    RuntimeSimulation runtime(context, scene);
    auto input = map().source();
    input["actions"][0]["kind"] = "axis1";
    runtime.input().configure(InputMap(input));
    runtime.input().submit({{"key.space", 1}});
    runtime.tick(1.f / 60);
    auto status = [&] { return runtime.input_status()["messages"]["sources"][0]; };
    check(status()["state"] == "Error" &&
              status()["diagnostic"] == "Input Message requires a digital action",
          "Axis action silently became an undeliverable emitter");
    input["version"] = 2;
    input["contexts"] = Json::array({{{"name", "Menu"},
                                      {"priority", 1},
                                      {"consume", true},
                                      {"active", true},
                                      {"phase", "control"}}});
    input["actions"][0]["kind"] = "digital";
    input["actions"][0]["context"] = "Menu";
    runtime.input().configure(InputMap(input));
    runtime.tick(1.f / 60);
    check(status()["state"] == "Error", "Control-frame action entered fixed message dispatch");
    runtime.input().configure(map());
    source.set<Payload>({std::string(70000, 'x'), 0});
    runtime.input().submit({{"key.space", 1}});
    runtime.tick(1.f / 60);
    check(status()["state"] == "Error" && runtime.input_status()["messages"]["emitted"] == 0,
          "Oversized managed payload was sent");
    configure_application_states(scene.world(),
                                 {{{"app",
                                    "Mode",
                                    "menu",
                                    {{"menu", "Menu", ""}, {"playing", "Playing", ""}},
                                    false,
                                    {},
                                    {}}}});
    initialize_application_states(scene.world());
    source.set<ApplicationStateGate>({true, "app", "playing"});
    runtime.tick(1.f / 60);
    check(status()["state"] == "Inactive", "State condition did not suppress messages");
    source.set<Payload>({"value", 0});
    control_application_states(scene.world(), {{"domain", "app"}, {"state", "playing"}});
    runtime.input().submit({{"key.space", 0}, {"key.space", 1}});
    runtime.tick(1.f / 60);
    check(status()["state"] == "Sent", "Active state condition did not resume messages");
}
} // namespace
int main() {
    try {
        authoring();
        dispatch();
        session_lifecycle();
        bounded_mutations();
        aggregate_values();
        invalid_routes_and_conditions();
        std::cout << "Input Message authoring, fixed dispatch, session lifecycle, bounds and "
                     "conditions passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
