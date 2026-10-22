#include <algorithm>
#include <forge/assets.hpp>
#include <forge/authoring.hpp>
#include <forge/prefab_authoring.hpp>
#include <forge/runtime.hpp>
#include <forge/runtime_ui.hpp>
#include <forge/ui_assets.hpp>
#include <fstream>
#include <iostream>
#include <thread>
using namespace forge;
void check(bool v, const char* why) {
    if (!v)
        throw std::runtime_error(why);
}
template <class F> void reject(F f) {
    try {
        f();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Expected rejection");
}
void check_ui_rows(const Scene& scene) {
    const auto authored = scene.snapshot();
    const auto revision = scene.revision();
    const auto full = scene.effective_document();
    auto expected = Json::array();
    for (const auto& row : full.at("entities")) {
        auto components = Json::object();
        if (row.at("components").contains("forge.ui_document"))
            components["forge.ui_document"] = row.at("components").at("forge.ui_document");
        expected.push_back({{"id", row.at("id")},
                            {"prefab", row.value("prefab", false)},
                            {"components", components}});
    }
    check(scene.ui_document_rows() == expected,
          "Narrow UI rows differ from the established effective Scene projection");
    check(scene.snapshot() == authored && scene.revision() == revision,
          "UI row projection mutated authored state or history");
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need test root");
        auto root = std::filesystem::path(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        const std::string rml =
            "<rml><head><style>body {font-family: Lato;}</style></head><body><button "
            "data-event-click=\"command('Pause')\">Pause</button></body></rml>";
        std::ofstream(root / "hud.rml") << rml;
        auto asset = register_ui_document(root, "hud.rml");
        check(register_ui_document(root, "hud.rml").id == asset.id,
              "Registration preserves identity");
        UiResources resources(root);
        check(resources.document({asset.id}) == "hud.rml", "Typed document resolution");
        reject([&] { resources.read("../escape.rml"); });
        reject([&] { resources.join("hud.rml", "https://host/file"); });
        reject([&] { validate_ui_text("<rml><body><div></body></rml>", true); });
        reject([&] { validate_ui_text("<rml><body><script>bad</script></body></rml>", true); });
        reject([&] { validate_ui_text("body { color: red;", false); });
        reject([&] { validate_ui_text("@import 'cycle.rcss';", false); });
        reject([&] { validate_ui_text(std::string(300000, 'x'), false); });
        reject([&] { validate_ui_text("body {font-size:999999px;}", false); });
        reject([&] {
            validate_ui_text("<rml><body style=\"font-size:999999px\"></body></rml>", true);
        });
        reject([&] { validate_ui_text("body {filter:blur(1px);}", false); });
        reject([&] { decode_ui_image({}); });
        reject([&] { validate_ui_font({}); });
        EngineContext author;
        check(!author.services().available(Capability::Ui), "Authoring has no UI runtime");
        EngineContext headless(WorldRole::Runtime);
        check(!headless.services().available(Capability::Ui), "Omitted UI has no runtime service");
        EngineContext runtime(WorldRole::Runtime, false, {ui_module(root)});
        Scene scene(runtime.world());
        scene.restore_snapshot(
            {{"version", 1},
             {"entities",
              Json::array({{{"id", "hud"}, {"name", "HUD"}, {"components", Json::object()}}})}});
        scene.entity("hud").set<UiDocument>({{asset.id}, true, true, 2});
        const auto entity = scene.reference("hud").entity;
        auto service = std::static_pointer_cast<UiRuntime>(runtime.services().ui());
        check_ui_rows(scene);
        {
            const auto unused = root / "unused-ui-project";
            std::filesystem::create_directories(unused);
            std::ofstream(unused / "forge.assets.json") << "{";
            EngineContext isolated(WorldRole::Runtime, false, {ui_module(unused)});
            Scene no_ui(isolated.world());
            check_ui_rows(no_ui);
            auto copied = std::static_pointer_cast<UiRuntime>(isolated.services().ui());
            auto state = copied->snapshot(no_ui, "isolated", 1, 0, true);
            check(state.at("documents").empty() && state.at("errors").empty(),
                  "An empty UI snapshot unnecessarily admitted project assets");
            no_ui.restore_snapshot({{"version", 1},
                                    {"entities", Json::array({{{"id", "unused"},
                                                               {"name", "Unused UI"},
                                                               {"components", Json::object()}},
                                                              {{"id", "unused2"},
                                                               {"name", "Second UI"},
                                                               {"components", Json::object()}}})}});
            no_ui.entity("unused").set<UiDocument>({{asset.id}, false, true, 0});
            check_ui_rows(no_ui);
            state = copied->snapshot(no_ui, "isolated", 1, 0, true);
            check(state.at("documents").empty() && state.at("errors").empty(),
                  "Disabled UI unnecessarily admitted project assets");
            no_ui.entity("unused").set<UiDocument>({{}, true, true, 0});
            state = copied->snapshot(no_ui, "isolated", 1, 0, true);
            check(state.at("documents").empty() && state.at("errors").empty(),
                  "Unassigned UI unnecessarily admitted project assets");
            // Visibility does not remove a live document's model/incarnation.
            no_ui.entity("unused").set<UiDocument>({{asset.id}, true, false, 0});
            state = copied->snapshot(no_ui, "isolated", 1, 0, true);
            check(state.at("documents").size() == 1 && state.at("errors").size() == 2,
                  "A required hidden UI document bypassed catalog admission");
            no_ui.entity("unused2").set<UiDocument>({{asset.id}, true, true, 0});
            no_ui.entity("unused2").add(flecs::Disabled);
            check_ui_rows(no_ui);
            state = copied->snapshot(no_ui, "isolated", 1, 0, true);
            check(state.at("documents").size() == 2 && state.at("errors").size() == 3,
                  "Failed catalog admission was retried for each UI document");
            std::filesystem::copy_file(root / "forge.assets.json", unused / "forge.assets.json",
                                       std::filesystem::copy_options::overwrite_existing);
            std::ofstream(unused / "hud.rml") << rml;
            state = copied->snapshot(no_ui, "isolated", 1, 0, true);
            check(state.at("errors").empty(), "UI did not retry a repaired catalog");
            const auto incarnation = state.at("documents")[0].at("instance");
            std::filesystem::remove(unused / "hud.rml");
            state = copied->snapshot(no_ui, "isolated", 1, 0, true);
            check(state.at("errors").size() == 2 &&
                      state.at("documents")[0].at("instance") == incarnation,
                  "UI cached stale availability or replaced a surviving document");
            std::ofstream(unused / "hud.rml") << rml;
            state = copied->snapshot(no_ui, "isolated", 1, 0, true);
            check(state.at("errors").empty() &&
                      state.at("documents")[0].at("instance") == incarnation,
                  "UI did not recover live availability without changing incarnation");
        }
        {
            EngineContext ordered_world;
            Scene ordered(ordered_world.world());
            const Json ui = {{"document", asset.id},
                             {"enabled", true},
                             {"visible", true},
                             {"layer", 3},
                             {"future", {{"keep", 7}}}};
            ordered.restore_snapshot(
                {{"version", 1},
                 {"entities",
                  Json::array(
                      {{{"id", "parent"}, {"name", "Parent"}, {"components", Json::object()}},
                       {{"id", "left"},
                        {"name", "Left"},
                        {"parent", "parent"},
                        {"components", {{"forge.ui_document", ui}}}},
                       {{"id", "right"},
                        {"name", "Right"},
                        {"parent", "parent"},
                        {"components", {{"forge.ui_document", ui}}}}})}});
            auto parent = ordered.entity("parent");
            parent.add(flecs::OrderedChildren);
            const ecs_entity_t reversed[] = {ordered.entity("right"), ordered.entity("left")};
            ecs_set_child_order(ordered.world(), parent, reversed, 2);
            check_ui_rows(ordered);
            check(ordered.ui_document_rows()[1].at("id").get<EntityId>() ==
                      ordered.reference("right").entity,
                  "UI rows lost native sibling ordering");
            PrefabLibrary library(root);
            const auto ui_prefab = library.create(
                ordered, create_prefab_source(ordered, ordered.reference("parent").entity.str()),
                "UI-rows.prefab.json");
            const auto instance = instantiate_prefab(ordered, ui_prefab);
            const auto original = library.source(ui_prefab);
            std::string member;
            for (const auto& row : original.at("members"))
                if (row.contains("parent") && row.at("components").contains("forge.ui_document")) {
                    member = row.at("id");
                    break;
                }
            check(!member.empty(), "UI Prefab child source is missing");
            std::string mapped;
            const auto instance_document = ordered.document();
            for (const auto& row : instance_document.at("entities"))
                if (row.at("id") == instance)
                    mapped = row.at("prefab_instance").at("members").at(member);
            check(!mapped.empty(), "UI child member was not mapped");
            authoring_command(ordered, "property.set",
                              {{"entity", mapped},
                               {"component", "forge.ui_document"},
                               {"field", "layer"},
                               {"value", 9}});
            check_ui_rows(ordered);
            auto removed = original;
            std::erase_if(removed["members"].get_ref<Json::array_t&>(),
                          [&](const Json& row) { return row.at("id") == member; });
            library.publish(ordered, original, removed);
            check(!ordered.entity(mapped), "Removed UI member remains native");
            check_ui_rows(ordered);
            bool retained = false;
            for (const auto& row : ordered.ui_document_rows())
                retained |= row.at("id") == mapped;
            check(retained, "UI rows omitted unresolved Prefab member intent");
            auto absent = library.source(ui_prefab), returned = absent;
            returned["members"] = original.at("members");
            library.publish(ordered, absent, returned);
            check(ordered.entity(mapped).has<UiDocument>(), "UI member did not return");
            check_ui_rows(ordered);
            ordered.entity(instance).destruct();
            check_ui_rows(ordered);
        }
        bool foreign_rejected = false;
        std::thread foreign([&] {
            try {
                service->publish(entity, "health", 1);
            } catch (const std::exception&) {
                foreign_rejected = true;
            }
        });
        foreign.join();
        check(foreign_rejected, "Retained UI service accepted foreign thread");
        service->publish(entity, "health", 100);
        service->allow_action("DecreaseHealth");
        auto state = service->snapshot(scene, "session", 1, 0, true);
        ui_protocol::validate_snapshot(state);
        check(state["documents"].size() == 1 && state["documents"][0]["model"]["health"] == 100,
              "Runtime owns model");
        ui_protocol::Replica replica;
        replica.reset("session", 1);
        check(replica.accept(state), "Accept authoritative snapshot");
        check(!replica.accept(state), "Reject repeated model revision");
        auto stale = state;
        stale["generation"] = 2u;
        check(!replica.accept(stale), "Reject stale-generation model");
        auto malformed = state;
        malformed["documents"][0]["model"]["pointer"] = Json::object();
        reject([&] { replica.accept(malformed); });
        ui_protocol::CommandGate gate;
        gate.reset("session", 1);
        Json command = {{"version", 1},
                        {"session", "session"},
                        {"generation", 1u},
                        {"id", 1u},
                        {"instance", state["documents"][0]["instance"]},
                        {"command", "DecreaseHealth"}};
        auto dispatch = [&](const Json& c) {
            service->command(scene, c, [](const std::string&) {});
        };
        auto ack = gate.dispatch(command, dispatch);
        check(ack["ok"], "UI command acknowledged");
        check(gate.dispatch(command, dispatch) == ack, "Exact retry repeats receipt");
        auto action = service->poll_action("DecreaseHealth");
        check(action && action->entity == entity && !service->poll_action("DecreaseHealth"),
              "Duplicate executes at most once");
        auto conflict = command;
        conflict["command"] = "Pause";
        reject([&] { gate.dispatch(conflict, dispatch); });
        stale = command;
        stale["generation"] = 2u;
        reject([&] { gate.dispatch(stale, dispatch); });
        command["id"] = 2u;
        command["command"] = "Unregistered";
        check(!gate.dispatch(command, dispatch)["ok"].get<bool>(),
              "Unregistered command rejected with receipt");
        command["id"] = 3u;
        command["command"] = "Pause";
        unsigned pauses = 0;
        check(gate.dispatch(command,
                            [&](const Json& c) {
                                service->command(scene, c, [&](const std::string&) { ++pauses; });
                            })["ok"],
              "Pause command succeeds");
        check(pauses == 1, "Runtime controls simulation policy");
        service->allow_value_action("SetPreference");
        auto value_command = command;
        value_command["command"] = "SetPreference";
        reject([&] { dispatch(value_command); });
        value_command["value"] = 42;
        reject([&] { dispatch(value_command); });
        value_command["value"] = "0.75";
        dispatch(value_command);
        auto preference = service->poll_action("SetPreference");
        check(preference && preference->value == "0.75", "Explicit value action lost its value");
        value_command["command"] = "DecreaseHealth";
        reject([&] { dispatch(value_command); });
        value_command["command"] = "Pause";
        reject([&] { dispatch(value_command); });
        // UI parses the existing bounded value transport; the clock owns rate admission.
        RuntimeClock clock;
        auto rate_command = command;
        rate_command["command"] = "TimeScale";
        auto rate_dispatch = [&](const Json& c) {
            service->command(
                scene, c, [](const std::string&) {},
                [&](double rate) { clock.set_time_scale(rate); });
        };
        for (const auto* value : {"0", "0.25", "0.5", "1", "2", "4"}) {
            rate_command["value"] = value;
            rate_dispatch(rate_command);
            check(clock.time_scale() == std::stod(value), "UI rate reaches clock owner");
        }
        const auto clock_before = clock.status();
        for (const Json& value : {Json("-1"), Json("4.01"), Json("nan"), Json("inf"), Json("1x"),
                                  Json(""), Json(true), Json(1), Json(nullptr)}) {
            rate_command["value"] = value;
            reject([&] { rate_dispatch(rate_command); });
            check(clock.status() == clock_before, "Invalid UI rate mutated the clock");
        }
        rate_command.erase("value");
        reject([&] { rate_dispatch(rate_command); });
        rate_command["value"] = "1";
        reject([&] { dispatch(rate_command); }); // Old caller has no rate owner.
        reject([&] { service->allow_action("TimeScale"); });
        reject([&] { service->allow_value_action("TimeScale"); });
        // All 29 existing custom slots remain available beside the four host actions.
        for (int i = 0; i != 27; ++i)
            service->allow_action("Custom" + std::to_string(i));
        reject([&] { service->allow_action("TooMany"); });
        auto full = service->snapshot(scene, "session", 1, 0, true);
        ui_protocol::validate_snapshot(full);
        check(full["documents"][0]["commands"].size() == 33,
              "TimeScale consumed a custom action slot");
        const auto saved = scene.document();
        check(saved.dump().find("health") == std::string::npos,
              "Presentation model is not authored state");
        EngineContext reopened;
        Scene reopened_scene(reopened.world());
        reopened_scene.restore_snapshot(saved);
        check(reopened_scene.entity(entity.str()).get<UiDocument>().document.id == asset.id,
              "UI scene roundtrip");
        auto changed = saved;
        for (auto& e : changed["entities"])
            if (e["id"] == entity.str())
                e["components"]["forge.ui_document"]["visible"] = false;
        scene.edit(changed);
        check(!scene.entity(entity.str()).get<UiDocument>().visible, "UI scene edit");
        scene.undo();
        check(scene.entity(entity.str()).get<UiDocument>().visible, "UI undo");
        scene.redo();
        check(!scene.entity(entity.str()).get<UiDocument>().visible, "UI redo");
        command["id"] = 4u;
        check(!gate.dispatch(command, dispatch)["ok"].get<bool>(), "Hidden documents reject input");
        auto catalog = AssetCatalog::open_project(root);
        std::filesystem::rename(root / "hud.rml", root / "moved.rml");
        catalog.relocate(asset.id, "moved.rml");
        catalog.save(AssetCatalog::project_index(root));
        UiResources renamed(root);
        check(renamed.document({asset.id}) == "moved.rml", "Asset rename preserves references");
        // Persistent prefab intent remains authoritative; presentation state stays out of it.
        EngineContext authored;
        Scene prefabs(authored.world());
        auto source = authoring_command(prefabs, "entity.create", {{"name", "UI source"}})
                          .at("selected")
                          .get<std::string>();
        authoring_command(prefabs, "component.add",
                          {{"entity", source}, {"component", "forge.ui_document"}});
        authoring_command(prefabs, "property.set",
                          {{"entity", source},
                           {"component", "forge.ui_document"},
                           {"field", "document"},
                           {"value", asset.id}});
        PrefabLibrary library(root);
        auto prefab =
            library.create(prefabs, create_prefab_source(prefabs, source), "UI.prefab.json");
        auto instance = instantiate_prefab(prefabs, prefab);
        check(!prefabs.entity(instance).owns<UiDocument>(), "UI prefab initially inherited");
        check_ui_rows(prefabs);
        authoring_command(prefabs, "property.set",
                          {{"entity", instance},
                           {"component", "forge.ui_document"},
                           {"field", "visible"},
                           {"value", true}});
        auto previous = library.source(prefab), next = previous;
        next["members"][0]["components"]["forge.ui_document"]["visible"] = false;
        next["members"][0]["components"]["forge.ui_document"]["layer"] = 4;
        library.publish(prefabs, previous, next);
        check(prefabs.entity(instance).get<UiDocument>().visible &&
                  prefabs.entity(instance).get<UiDocument>().layer == 4,
              "Equal UI override preserved; other fields propagate");
        check_ui_rows(prefabs);
        authoring_command(
            prefabs, "property.revert",
            {{"entity", instance}, {"component", "forge.ui_document"}, {"field", "visible"}});
        check(!prefabs.entity(instance).get<UiDocument>().visible,
              "UI property Revert follows source");
        check(prefabs.undo() && prefabs.entity(instance).get<UiDocument>().visible,
              "UI Revert undo");
        check(prefabs.redo() && !prefabs.entity(instance).get<UiDocument>().visible,
              "UI Revert redo");
        auto duplicated_instance = prefabs.duplicate_subtree(instance);
        check(prefabs.entity(duplicated_instance).get<UiDocument>().document.id == asset.id,
              "Duplicate UI instance retains asset");
        auto duplicate = library.duplicate(prefabs, prefab, "UI-copy.prefab.json");
        auto copy = instantiate_prefab(prefabs, duplicate);
        check(prefabs.entity(copy).get<UiDocument>().document.id == asset.id,
              "Prefab asset duplicate retains logical UI reference");
        EngineContext loaded;
        Scene loaded_scene(loaded.world());
        PrefabLibrary reopened_prefabs(root);
        reopened_prefabs.load_scene(loaded_scene, prefabs.document());
        check(loaded_scene.entity(instance).get<UiDocument>().layer == 4, "UI prefab reopen");
        // Disabling/re-enabling creates a fresh document incarnation, even with the same asset.
        auto ui = scene.entity(entity.str()).get<UiDocument>();
        ui.enabled = false;
        scene.entity(entity.str()).set(ui);
        service->snapshot(scene, "session", 1, 0, true);
        ui.enabled = true;
        ui.visible = true;
        scene.entity(entity.str()).set(ui);
        auto newer = service->snapshot(scene, "session", 1, 0, true);
        check(newer["documents"][0]["instance"] != command["instance"],
              "Document incarnation renewed");
        reject([&] { service->command(scene, command, [](const auto&) {}); });
        reject([&] { service->publish(entity, "forge_loading_state", "fake ready"); });
        service->shutdown();
        reject([&] { service->publish(entity, "health", 5); });
        reject([&] { service->snapshot(scene, "stopped", 1, 0, true); });
        std::filesystem::remove_all(root);
        std::cout
            << "Runtime UI identity, admission, lifetime, history and protocol checks passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
