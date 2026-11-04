#include "authored_schema.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <forge/application_state_components.hpp>
#include <forge/application_states.hpp>
#include <forge/game_session.hpp>
#include <forge/input_message.hpp>
#include <forge/runtime.hpp>
#include <iostream>
#include <map>

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
    edited["entities"][0]["components"]["forge.input_message"]["trigger"] = 0;
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
    for (const auto& invalid : {Json(-1), Json(3), Json(std::uint64_t{1} << 32), Json(1.5),
                                Json(true), Json("Changed")}) {
        auto bad = edited;
        bad["entities"][0]["components"]["forge.input_message"]["trigger"] = invalid;
        rejects([&] { scene.edit(bad); });
        check(scene.document() == edited, "Rejected trigger changed source/history");
    }
    bool found = false;
    for (const auto& type : context.schema().at("components"))
        if (type.at("id") == "forge.input_message") {
            found = true;
            check(type.at("optional") == true && type.at("category") == "Gameplay",
                  "Input Message schema is not discoverable");
            const auto trigger =
                std::find_if(type.at("fields").begin(), type.at("fields").end(),
                             [](const auto& field) { return field.at("id") == "trigger"; });
            check(trigger != type.at("fields").end() && trigger->at("minimum") == 0 &&
                      trigger->at("maximum") == 2 && trigger->at("default") == 0 &&
                      trigger->at("default_if_missing") == 0,
                  "Trigger discovery lost range or legacy default");
        }
    check(found, "Input Message metadata missing");
}
void prefab_trigger_history() {
    EngineContext engine(WorldRole::Runtime);
    Scene scene(engine.world());
    const auto base = AssetId::generate(), variant = AssetId::generate();
    const auto member = PrefabMemberId::generate();
    const Json original{
        {"format", "forge.prefab"},
        {"version", 1},
        {"asset_id", base},
        {"revision", 1},
        {"root", member},
        {"members",
         Json::array(
             {{{"id", member},
               {"name", "Source"},
               {"components",
                {{"forge.tint", {{"r", .2}, {"g", .3}, {"b", .4}}},
                 {"forge.input_message",
                  {{"enabled", true}, {"action", action}, {"payload", "forge.tint"}}}}}}})}};
    const Json derived{
        {"format", "forge.prefab"},
        {"version", 4},
        {"asset_id", variant},
        {"revision", 1},
        {"root", member},
        {"base", base},
        {"overrides",
         Json::array({{{"id", member},
                       {"components", Json::object()},
                       {"property_overrides", {{"forge.input_message", {{"trigger", 1}}}}}}})}};
    const PrefabSources sources{{base, original}, {variant, derived}};
    scene.set_prefab_sources(sources);
    auto document = scene.document();
    document["version"] = 4;
    std::array<std::string, 2> ids;
    for (unsigned i = 0; i < ids.size(); ++i) {
        ids[i] = EntityId::generate().str();
        document["entities"].push_back({{"id", ids[i]},
                                        {"name", "Instance"},
                                        {"components", Json::object()},
                                        {"prefab_instance",
                                         {{"asset", i == 0 ? base : variant},
                                          {"revision", 1},
                                          {"members", {{member.str(), ids[i]}}}}}});
    }
    scene.edit(reconcile_prefab_intent(document, sources, scene.schema()));
    check(scene.entity(ids[0]).get<InputMessage>().trigger == 0 &&
              scene.entity(ids[1]).get<InputMessage>().trigger == 1 &&
              !scene.entity(ids[0]).owns<InputMessage>() &&
              !scene.entity(ids[1]).owns<InputMessage>(),
          "Legacy Prefab default or variant trigger inheritance changed");
    const auto inherited = scene.document();
    auto overridden = inherited;
    overridden["entities"][1]["property_overrides"]["forge.input_message"]["trigger"] = 2;
    scene.edit(overridden);
    overridden = scene.document();
    check(scene.entity(ids[1]).get<InputMessage>().trigger == 2 && scene.undo() &&
              scene.document() == inherited &&
              scene.entity(ids[1]).get<InputMessage>().trigger == 1 && scene.redo() &&
              scene.document() == overridden,
          "Instance trigger override lost shared history or inherited values");
    check(scene.prefab_sources() == sources,
          "Instance trigger override rewrote Prefab source intent");
    unsigned calls = 0;
    scene.world().observer().event<Tint>().with(flecs::Any).each([&](flecs::iter&, std::size_t) {
        ++calls;
    });
    RuntimeSimulation runtime(engine.world(), scene);
    runtime.input().configure(map());
    runtime.input().submit({{"key.space", 1}});
    runtime.tick(1.f / 60);
    check(calls == 2, "Inherited Pressed/Changed sources or payloads did not dispatch");
    runtime.input().submit({{"key.space", 0}});
    runtime.tick(1.f / 60);
    check(calls == 3, "Inherited Changed source did not dispatch release");
    WorldContext restored_world;
    Scene restored(restored_world);
    restored.restore_snapshot(scene.snapshot());
    check(restored.document() == scene.document() &&
              restored.entity(ids[0]).get<InputMessage>().trigger == 0 &&
              restored.entity(ids[1]).get<InputMessage>().trigger == 2,
          "Prefab trigger snapshot changed identity, intent or effective values");
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
void trigger_modes() {
    EngineContext engine(WorldRole::Runtime);
    Scene scene(engine.world());
    Json entities = Json::array();
    for (const auto* name : {"pressed", "released", "changed", "axis1", "axis2"})
        entities.push_back({{"id", name}, {"name", name}, {"components", Json::object()}});
    scene.replace({{"version", 1}, {"entities", entities}});
    auto source = map().source();
    const auto axis1 = ActionId::generate(), axis2 = ActionId::generate();
    source["actions"].push_back(
        {{"id", axis1},
         {"name", "Axis one"},
         {"kind", "axis1"},
         {"bindings", Json::array({{{"control", "pad.left_x"}, {"deadzone", 0}}})}});
    source["actions"].push_back(
        {{"id", axis2},
         {"name", "Axis two"},
         {"kind", "axis2"},
         {"bindings",
          Json::array({{{"control", "pad.left_x"}, {"x", 1}, {"y", 0}, {"deadzone", 0}},
                       {{"control", "pad.left_y"}, {"x", 0}, {"y", 1}, {"deadzone", 0}}})}});
    const auto configure = [&](const char* name, const std::string& id,
                               InputMessageTrigger trigger) {
        scene.entity(name)
            .set<Tint>({.2f, .3f, .4f})
            .set<InputMessage>({true, id, "forge.tint", std::uint32_t(trigger)});
    };
    configure("pressed", action, InputMessageTrigger::Pressed);
    configure("released", action, InputMessageTrigger::Released);
    configure("changed", action, InputMessageTrigger::Changed);
    configure("axis1", axis1.str(), InputMessageTrigger::Changed);
    configure("axis2", axis2.str(), InputMessageTrigger::Changed);
    RuntimeSimulation runtime(engine.world(), scene);
    runtime.input().configure(InputMap(source));
    std::map<std::string, unsigned> calls;
    std::map<std::string, ActionState> samples;
    bool values = true;
    scene.world()
        .observer()
        .event<Tint>()
        .with(flecs::Any)
        .each([&](flecs::iter& it, std::size_t row) {
            const auto entity = it.entity(row);
            const auto name = entity.get<AuthoredName>().value;
            const auto* value = it.param<Tint>();
            values &= value && std::abs(value->r - .2f) < 1e-6;
            ++calls[name];
            const auto id = ActionId::parse(entity.get<InputMessage>().action);
            samples[name] = runtime.input().snapshot().actions.at(id);
        });
    const auto tick = [&] { runtime.tick(1.f / 60); };
    const auto counts = [&](std::array<unsigned, 5> expected) {
        check(values && calls["pressed"] == expected[0] && calls["released"] == expected[1] &&
                  calls["changed"] == expected[2] && calls["axis1"] == expected[3] &&
                  calls["axis2"] == expected[4],
              "Pressed/released/changed fixed input delivery differs");
    };
    tick();
    counts({0, 0, 0, 0, 0});
    runtime.input().submit({{"key.space", 1}, {"pad.left_x", .3}, {"pad.left_y", .4}});
    tick();
    counts({1, 0, 1, 1, 1});
    check(samples["axis1"].x == .3 && samples["axis2"].x == .3 && samples["axis2"].y == .4,
          "Message observers did not borrow the same fixed analog input values");
    tick();
    counts({1, 0, 1, 1, 1});
    runtime.input().submit({{"key.space", 0}, {"pad.left_x", .3}, {"pad.left_y", .6}});
    tick();
    counts({1, 1, 2, 1, 2});
    runtime.input().submit(
        {{"key.space", 1}, {"key.space", 0}, {"pad.left_x", 0}, {"pad.left_y", 0}});
    tick();
    counts({2, 2, 3, 2, 3});
    check(samples["changed"].pressed && samples["changed"].released && !samples["changed"].held &&
              samples["axis1"].x == 0 && samples["axis2"].x == 0 && samples["axis2"].y == 0,
          "Quick-tap coalescing or analog return to neutral was lost");
    tick();
    counts({2, 2, 3, 2, 3});
}
void changed_route_lifetime() {
    EngineContext engine(WorldRole::Runtime);
    Scene scene(engine.world());
    scene.replace(
        {{"version", 1},
         {"entities",
          Json::array({{{"id", "source"}, {"name", "Source"}, {"components", Json::object()}}})}});
    const auto source = scene.entity("source");
    const auto axis = ActionId::generate();
    InputMessage config{true, axis.str(), "forge.tint",
                        std::uint32_t(InputMessageTrigger::Changed)};
    source.set<InputMessage>(config).set<Tint>({.2f, .3f, .4f});
    RuntimeSimulation runtime(engine.world(), scene);
    runtime.input().configure(
        InputMap({{"version", 2},
                  {"contexts", Json::array({{{"name", "Game"}, {"active", true}}})},
                  {"actions", Json::array({{{"id", axis},
                                            {"name", "Axis"},
                                            {"kind", "axis1"},
                                            {"context", "Game"},
                                            {"bindings", Json::array({{{"control", "pad.left_x"},
                                                                       {"deadzone", 0}}})}}})}}));
    unsigned calls = 0;
    scene.world().observer().event<Tint>().with(flecs::Any).each([&](flecs::iter&, std::size_t) {
        ++calls;
    });
    const auto tick = [&] { runtime.tick(1.f / 60); };
    const auto state = [&] {
        return runtime.input_status().at("messages").at("sources").at(0).at("state");
    };
    tick();
    check(calls == 0, "Initial neutral route emitted");
    runtime.input().submit({{"pad.left_x", .4}});
    tick();
    check(calls == 1, "Initial nonneutral route did not emit");
    config.enabled = false;
    source.set<InputMessage>(config);
    runtime.input().submit({{"pad.left_x", .7}});
    tick();
    check(calls == 1 && state() == "Inactive", "Disabled changed route emitted");
    config.enabled = true;
    source.set<InputMessage>(config);
    tick();
    check(calls == 2, "Reenabled route retained an inactive observation");
    source.remove<InputMessage>();
    tick();
    check(calls == 2 && runtime.input_status()["messages"]["sources"].empty(),
          "Removed emitter retained a status row");
    source.set<InputMessage>(config);
    tick();
    check(calls == 3, "Reattached emitter reused its retired observation");
    runtime.input().activate_contexts({});
    tick();
    check(calls == 4, "Context neutralization did not emit the changed neutral value");
    tick();
    check(calls == 4, "Stable neutral context repeated a message");
    runtime.input().activate_contexts({"Game"});
    runtime.input().submit({{"pad.left_x", .7}});
    tick();
    check(calls == 5, "Reactivated context did not observe its new value");
    config.trigger = std::uint32_t(InputMessageTrigger::Released);
    source.set<InputMessage>(config);
    tick();
    check(calls == 5 && state() == "Error", "Released mode accepted an analog route");
    config.trigger = std::uint32_t(InputMessageTrigger::Changed);
    source.set<InputMessage>(config);
    tick();
    check(calls == 6, "Changed route reused observation from an invalid trigger mode");
    source.remove<Tint>();
    runtime.input().submit({{"pad.left_x", .9}});
    tick();
    check(calls == 6 && state() == "Error", "Missing payload did not reject changed delivery");
    source.set<Tint>({.2f, .3f, .4f});
    tick();
    check(calls == 6 && state() == "Ready", "Restored payload replayed an old input change");
    runtime.input().submit({{"pad.left_x", .95}});
    tick();
    check(calls == 7 && state() == "Sent", "Restored payload lost the next genuine input change");
    const auto retired = scene.entity("source").id();
    scene.delete_subtree("source");
    check(scene.undo(), "Deleted source could not be recreated through Scene history");
    check(scene.entity("source").id() != retired,
          "Recreated source retained a retired native handle");
    tick();
    check(calls == 8, "Recreated durable source reused the old native generation observation");
    configure_application_states(scene.world(),
                                 {{{"app",
                                    "Mode",
                                    "menu",
                                    {{"menu", "Menu", ""}, {"playing", "Playing", ""}},
                                    false,
                                    {},
                                    {}}}});
    initialize_application_states(scene.world());
    scene.entity("source").set<ApplicationStateGate>({true, "app", "playing"});
    tick();
    check(calls == 8 && state() == "Inactive", "Inactive state gate emitted Changed");
    control_application_states(scene.world(), {{"domain", "app"}, {"state", "playing"}});
    tick();
    check(calls == 9, "State reactivation retained the inactive route observation");
    auto rebound = runtime.input().map().with_bindings(
        axis, Json::array({{{"control", "pad.right_x"}, {"deadzone", 0}}}));
    runtime.input().replace_map(rebound);
    tick();
    check(calls == 10, "Input map replacement lost its changed neutral value");
    runtime.input().submit({{"pad.left_x", .5}});
    tick();
    check(calls == 10, "Old binding still drove Changed after map replacement");
    runtime.input().submit({{"pad.right_x", .3}});
    tick();
    check(calls == 11, "Replacement binding did not drive Changed");
    rebound = rebound.with_bindings(axis, Json::array({{{"control", "mouse.delta_x"}}}));
    runtime.input().replace_map(rebound);
    tick();
    check(calls == 12, "Relative route replacement lost its neutral value");
    runtime.input().submit({{"mouse.delta_x", .25}});
    tick();
    check(calls == 13, "Relative input value change was lost");
    runtime.input().submit({{"mouse.delta_x", .25}});
    tick();
    check(calls == 13, "Equal consecutive relative values repeated Changed");
    tick();
    check(calls == 14, "Relative input return to zero did not emit Changed");
    tick();
    check(calls == 14, "Stable zero relative input repeated Changed");
}
void changed_source_bound() {
    EngineContext engine(WorldRole::Runtime);
    Scene scene(engine.world());
    Json entities = Json::array();
    for (unsigned i = 0; i != 257; ++i)
        entities.push_back({{"id", "source" + std::to_string(i)},
                            {"name", "Source"},
                            {"components",
                             {{"forge.tint", {{"r", .2}, {"g", .3}, {"b", .4}}},
                              {"forge.input_message",
                               {{"enabled", true},
                                {"action", action},
                                {"payload", "forge.tint"},
                                {"trigger", 2}}}}}});
    scene.replace({{"version", 1}, {"entities", entities}});
    RuntimeSimulation runtime(engine.world(), scene);
    runtime.input().configure(map());
    runtime.input().submit({{"key.space", 1}});
    runtime.tick(1.f / 60);
    auto status = runtime.input_status().at("messages");
    check(status.at("emitted") == 256 && status.at("overflow") == 1,
          "Changed source batch did not retain its existing bound");
    const auto removed = status.at("sources")[0].at("entity").get<EntityRef>();
    scene.delete_subtree(removed.entity.str());
    runtime.tick(1.f / 60);
    status = runtime.input_status().at("messages");
    check(status.at("emitted") == 1 && status.at("overflow") == 0,
          "Newly considered source reused omitted observations or repeated stable sources");
    runtime.input().submit({{"key.space", 0}});
    runtime.tick(1.f / 60);
    check(runtime.input_status().at("messages").at("emitted") == 256,
          "Bounded Changed routes lost the release transition");
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
        prefab_trigger_history();
        dispatch();
        trigger_modes();
        changed_route_lifetime();
        changed_source_bound();
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
