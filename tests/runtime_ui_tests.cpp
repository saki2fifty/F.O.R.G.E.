#include <forge/authoring.hpp>
#include <forge/prefab_authoring.hpp>
#include <forge/runtime_ui.hpp>
#include <forge/ui_assets.hpp>
#include <fstream>
#include <iostream>
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
        service->shutdown();
        reject([&] { service->publish(entity, "health", 5); });
        std::filesystem::remove_all(root);
        std::cout
            << "Runtime UI identity, admission, lifetime, history and protocol checks passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
