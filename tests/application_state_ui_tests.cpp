#include "application_state_fixture.hpp"
#include "asset_bytes.hpp"
#include "ui_test_renderer.hpp"
#include <RmlUi/Core/ElementText.h>
#include <forge/application_states.hpp>
#include <forge/runtime_ui.hpp>
#include <iostream>

namespace {
using namespace forge;
void check(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F&& call) {
    try {
        call();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid state UI operation accepted");
}
std::string text(Rml::ElementDocument* document, const char* id) {
    const auto element = document->GetElementById(id);
    check(element && element->GetNumChildren() == 1 &&
              element->GetChild(0)->GetTagName() == "#text",
          "Expected literal native text");
    return static_cast<Rml::ElementText*>(element->GetChild(0))->GetText();
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need scratch directory and packaged font");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        const auto authored = forge::test::application_state_fixture(root);
        atomic_write(root / "states.rml", R"rml(<rml><head><style>
body, p {display:block;} body {font-family:Lato;font-size:18px;color:#fff;}
button,input {display:block;width:200px;height:36px;}
</style></head><body>
<p id="active">{{forge_state_app}} / {{forge_rev_app}}</p>
<p id="computed">{{forge_state_hud}}</p>
<p id="nested">{{forge_state_level}}</p>
<p id="parent">{{forge_state_app | state_is('app','game')}}</p>
<button id="start" data-event-click="command('forge.state', 'app:playing:' + forge_rev_app)">Start</button>
<button id="back" data-event-click="command('forge.state', 'app:menu:' + forge_rev_app)">Menu</button>
<input id="field" type="text"/>
<button id="write" data-event-click="forge_state_app='forged'">Try readonly write</button>
</body></rml>)rml");
        const auto asset = register_ui_document(root, "states.rml");
        auto lease = std::make_shared<ProjectLease>(root);
        ApplicationStatesDocument source(lease, "Assets/Modes.appstates.json");
        auto definition = source.source().compiled();
        EngineContext engine(WorldRole::Runtime, false, {ui_module(root)});
        Scene scene(engine.world());
        auto scene_source = authored;
        const auto hud_id = EntityId::generate();
        scene_source["entities"].push_back(
            {{"id", hud_id}, {"name", "HUD"}, {"components", Json::object()}});
        scene.restore_snapshot(scene_source);
        auto hud = scene.entity(hud_id.str());
        hud.set<UiDocument>({{asset.id}, true, true, 0});
        configure_application_states(scene.world(), definition, source.source().asset());
        initialize_application_states(scene.world());
        auto ui = std::static_pointer_cast<UiRuntime>(engine.services().ui());
        auto state = ui->snapshot(scene, "state-ui", 1, 42, true);
        check(state.at("application_states").at("domains")[2].at("active") == "",
              "Inactive subordinate was fabricated");
        for (const auto* invalid :
             {"", "app:playing", "app:playing:", "app:playing:-1", "app:playing:1x",
              "app:playing:18446744073709551616", "app:bad:key:1"})
            rejects([&] { (void)ui_protocol::application_state_request(invalid); });
        UiTestRenderer renderer;
        {
            UiPresenter presenter(renderer, root,
                                  asset_detail::read_bytes(argv[2], 4 * 1024 * 1024));
            presenter.reset("state-ui", 1);
            check(presenter.accept(state), presenter.diagnostic());
            presenter.update(.1, 800, 600);
            presenter.render();
            auto context = Rml::GetContext(0);
            auto dom = context->GetDocument(0);
            check(text(dom, "active") == "menu / 1" && text(dom, "computed") == "hidden" &&
                      text(dom, "parent") == "0",
                  "Initial native state bindings are wrong");
            const auto instance = state.at("documents")[0].at("instance");
            ui_protocol::CommandGate gate;
            gate.reset("state-ui", 1);
            auto click = [&](const char* id) {
                dom->GetElementById(id)->DispatchEvent("click", Rml::Dictionary{});
                check(presenter.pending_command().has_value(),
                      "Native state button did not queue command");
                auto command = *presenter.pending_command();
                auto ack = gate.dispatch(command, [&](const Json& request) {
                    ui->command(scene, request, [](const auto&) {
                        throw std::runtime_error("State action advanced host clock");
                    });
                });
                check(ack.at("ok"), ack.dump());
                presenter.acknowledge(ack);
                check(gate.dispatch(command,
                                    [](const Json&) {
                                        throw std::runtime_error(
                                            "Duplicate UI state action executed");
                                    }) == ack,
                      "Duplicate receipt changed");
                return command;
            };
            auto command = click("start");
            check(command.at("value") == "app:playing:1",
                  "Native revision concatenation lost precision or syntax");
            auto field = dom->GetElementById("field");
            check(field->Focus(), "Native field did not focus");
            presenter.text("retained");
            state = ui->snapshot(scene, "state-ui", 1, 42, true);
            check(state.at("documents")[0].at("instance") == instance,
                  "State transition renewed document authority");
            check(presenter.accept(state), presenter.diagnostic());
            presenter.update(.2, 800, 600);
            check(Rml::GetContext(0) == context && context->GetFocusElement() == field,
                  "State transition discarded native focus/context");
            check(text(dom, "active") == "playing / 2" && text(dom, "computed") == "visible" &&
                      text(dom, "nested") == "warmup" && text(dom, "parent") == "1",
                  "Hierarchy/computed/subordinate did not update native bindings");
            rejects([&] { ui->command(scene, command, [](const auto&) {}); });
            check(application_state_current(scene.world(), "app") == "playing",
                  "Stale UI request changed state");
            click("back");
            state = ui->snapshot(scene, "state-ui", 1, 42, true);
            check(presenter.accept(state), presenter.diagnostic());
            presenter.update(.3, 800, 600);
            check(text(dom, "active") == "menu / 3" && text(dom, "computed") == "hidden",
                  "Native retry did not use latest revision");
            auto observed = state.at("application_states");
            observed.erase("controller");
            check(observed == application_states_status(scene.world()),
                  "UI observations differ from their core owner");
            dom->GetElementById("write")->DispatchEvent("click", Rml::Dictionary{});
            presenter.update(.4, 800, 600);
            check(text(dom, "active") == "menu / 3" &&
                      application_state_current(scene.world(), "app") == "menu" &&
                      !presenter.pending_command(),
                  "Native expression wrote a readonly state binding");
            auto bad_candidate = ui->snapshot(scene, "state-ui", 1, 42, true);
            bad_candidate["documents"][0]["model"]["forge_state_app"] = "spoofed";
            const auto displayed = presenter.application_states_state();
            check(!presenter.accept(bad_candidate) && Rml::GetContext(0) == context &&
                      presenter.application_states_state() == displayed,
                  "Failed state binding admission replaced the displayed set");
            state = ui->snapshot(scene, "state-ui", 1, 42, true);
            check(presenter.accept(state), presenter.diagnostic());
            {
                EngineContext other(WorldRole::Runtime, false, {ui_module(root)});
                Scene copy(other.world());
                copy.restore_snapshot(scene.snapshot());
                configure_application_states(copy.world(), definition, source.source().asset());
                initialize_application_states(copy.world());
                control_application_states(copy.world(), {{"domain", "app"}, {"state", "playing"}});
                auto other_ui = std::static_pointer_cast<UiRuntime>(other.services().ui());
                const auto next = other_ui->snapshot(copy, "state-ui", 2, 42, true);
                check(next.at("application_states").at("domains")[0].at("active") == "playing" &&
                          application_state_current(scene.world(), "app") == "menu",
                      "Independent Worlds shared UI state authority");
                const auto ticket = presenter.prepare(next);
                check(presenter.prepared(ticket) && Rml::GetNumContexts() == 2 &&
                          presenter.application_states_state() == displayed &&
                          text(dom, "active") == "menu / 3",
                      "Prepared state observations leaked into the active context");
                presenter.cancel_prepared(ticket);
                check(Rml::GetNumContexts() == 1, "Canceled state context retained its bindings");
                other_ui->allow_value_action("forge.state");
                rejects([&] { (void)other_ui->snapshot(copy, "state-ui", 2, 42, true); });
            }
            const auto modes = scene.entity(authored.at("entities")[0].at("id").get<std::string>());
            const auto config = modes.get<ApplicationStates>();
            modes.remove<ApplicationStates>();
            rejects([&] { (void)ui->snapshot(scene, "state-ui", 1, 42, true); });
            rejects([&] { ui->command(scene, command, [](const auto&) {}); });
            hud.set(config);
            state = ui->snapshot(scene, "state-ui", 1, 42, true);
            check(state.at("documents")[0].at("instance") != instance,
                  "Changed state controller retained old input authority");
            rejects([&] { ui->command(scene, command, [](const auto&) {}); });
            check(presenter.accept(state) &&
                      presenter.application_states_state().at("controller") == Json(hud_id),
                  "New controller did not publish a complete state presentation");
        }
        check(renderer.geometry.empty() && renderer.textures.empty(),
              "State UI retained native render handles");
        for (unsigned i = 0; i < 29; ++i)
            ui->allow_action("custom_" + std::to_string(i));
        state = ui->snapshot(scene, "state-ui", 1, 42, true);
        check(state.at("documents")[0].at("commands").size() == 34,
              "State controls consumed an existing custom action slot");
        auto invalid = state;
        invalid["application_states"]["version"] = 1.0;
        rejects([&] { ui_protocol::validate_snapshot(invalid); });
        invalid = state;
        invalid["application_states"]["source_asset"] = nullptr;
        rejects([&] { ui_protocol::validate_snapshot(invalid); });
        auto localized = state;
        localized["localization"] = {
            {"entity", EntityId::generate()}, {"asset", AssetId::generate()}, {"locale", "en"}};
        localized["documents"][0]["commands"].push_back("forge.locale");
        ui_protocol::validate_snapshot(localized);
        localized["documents"][0]["commands"].push_back("extra_custom");
        rejects([&] { ui_protocol::validate_snapshot(localized); });
        invalid = state;
        invalid.erase("application_states");
        rejects([&] { ui_protocol::validate_snapshot(invalid); });
        invalid = state;
        invalid["application_states"]["domains"][0]["revision"] = -1;
        rejects([&] { ui_protocol::validate_snapshot(invalid); });
        invalid = state;
        invalid["application_states"]["domains"][0]["membership"].push_back("menu");
        rejects([&] { ui_protocol::validate_snapshot(invalid); });
        ui->publish(hud_id, "forge_state_app", "spoofed");
        rejects([&] { (void)ui->snapshot(scene, "state-ui", 1, 42, true); });
        {
            EngineContext unconfigured(WorldRole::Runtime, false, {ui_module(root)});
            Scene legacy(unconfigured.world());
            legacy.restore_snapshot(scene.snapshot());
            auto custom = std::static_pointer_cast<UiRuntime>(unconfigured.services().ui());
            custom->allow_value_action("forge.state");
            custom->publish(hud_id, "forge_state_app", "custom");
            const auto snapshot = custom->snapshot(legacy, "custom-ui", 1, 42, true);
            check(!snapshot.contains("application_states"),
                  "Unconfigured UI gained state authority");
            custom->command(legacy,
                            {{"instance", snapshot.at("documents")[0].at("instance")},
                             {"command", "forge.state"},
                             {"value", "legacy custom action"}},
                            [](const auto&) {});
            check(custom->poll_action("forge.state").has_value(),
                  "Unconfigured project lost its existing custom action");
        }
        std::cout << "Native state UI readonly bindings, revision controls, hierarchy, scope, "
                     "focus, command replay and limits passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
