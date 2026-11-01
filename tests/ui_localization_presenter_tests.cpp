#include "asset_bytes.hpp"
#include "ui_localization_fixture.hpp"
#include "ui_test_renderer.hpp"
#include <RmlUi/Core/ElementText.h>
#include <forge/runtime_ui.hpp>
#include <iostream>

using namespace forge;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
std::string text(Rml::ElementDocument* document, const char* id) {
    const auto* element = document->GetElementById(id);
    check(element && element->GetNumChildren() == 1, "Expected one native literal text child");
    const auto* child = element->GetChild(0);
    check(child->GetTagName() == "#text", "Translation introduced a native markup element");
    return static_cast<const Rml::ElementText*>(child)->GetText();
}
UiLocalizationSource messages(AssetId id) {
    auto source = UiLocalizationSource::create(id);
    source.document["locales"]["en"]["messages"]["literal"] =
        "Crème <b>café</b> & {{counter}} &lt;";
    return source;
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need presenter scratch root and packaged font");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "Assets");
        atomic_write(root / "hud.rml", R"rml(<rml><head><style>
body, div, p {display:block;} body {font-family:Lato;font-size:18px;color:#fff;}
button {display:block;width:160px;height:32px;} input {display:block;width:180px;height:32px;}
</style></head><body>
<p id="title">{{ 'menu_play' | tr(forge_locale) }}</p>
<p id="literal">{{ 'literal' | tr(forge_locale) }}</p>
<p id="missing">{{ 'absent' | tr(forge_locale) }}</p>
<p id="language">{{forge_locale}} / {{forge_resolved_locale}}</p>
<input id="field" type="text"/>
<button id="french" data-event-click="command('forge.locale', 'fr')">Français</button>
</body></rml>)rml");
        const auto document = register_ui_document(root, "hud.rml");
        const auto lease = std::make_shared<ProjectLease>(root);
        const auto first =
            test_ui_localization::publish(lease, "Assets/First.localization.json", messages);
        const auto second =
            test_ui_localization::publish(lease, "Assets/Second.localization.json", [](AssetId id) {
                auto source = messages(id);
                source.document["locales"]["fr"]["messages"]["menu_play"] = "Continuer";
                return source;
            });
        EngineContext engine(WorldRole::Runtime, false, {ui_module(root)});
        Scene scene(engine.world());
        scene.restore_snapshot(
            {{"version", 1},
             {"entities",
              Json::array(
                  {{{"id", "hud"}, {"name", "HUD"}, {"components", Json::object()}},
                   {{"id", "language"}, {"name", "Language"}, {"components", Json::object()}}})}});
        scene.entity("hud").set<UiDocument>({{document.id}, true, true, 0});
        scene.entity("language").set<UiLocalization>({{first}, "en"});
        auto ui = std::static_pointer_cast<UiRuntime>(engine.services().ui());
        ui->publish(scene.reference("hud").entity, "counter", 999);
        auto state = ui->snapshot(scene, "native-localization", 1, 0, true);
        UiTestRenderer renderer;
        {
            UiPresenter presenter(renderer, root,
                                  asset_detail::read_bytes(argv[2], 4 * 1024 * 1024));
            presenter.reset("native-localization", 1);
            check(presenter.accept(state), presenter.diagnostic().c_str());
            presenter.update(.1, 800, 600);
            presenter.render();
            auto* context = Rml::GetContext(0);
            auto* dom = context->GetDocument(0);
            check(text(dom, "title") == "Play" && text(dom, "missing") == "[[absent]]",
                  "Native text did not receive translated or missing-key values");
            check(text(dom, "literal") == "Crème <b>café</b> & {{counter}} &lt;",
                  "Translation interpreted markup, entities or another data expression");
            check(renderer.draws > 0 && !renderer.textures.empty(),
                  "Native layout/font geometry did not reach the recording render interface");
            auto* field = dom->GetElementById("field");
            dom->GetElementById("french")->DispatchEvent("click", Rml::Dictionary{});
            check(presenter.pending_command().has_value(),
                  "Native locale button did not queue a command");
            ui_protocol::CommandGate gate;
            gate.reset("native-localization", 1);
            const auto ack = gate.dispatch(*presenter.pending_command(), [&](const auto& command) {
                ui->command(scene, command, [](const auto&) {
                    throw std::runtime_error("Locale command advanced a clock control");
                });
            });
            check(ack.at("ok"), "Native locale command was rejected");
            presenter.acknowledge(ack);
            check(field->Focus() && presenter.wants_text(), "Native text field did not focus");
            presenter.text("kept");
            const auto revision = presenter.presentation_revision();
            state = ui->snapshot(scene, "native-localization", 1, 0, true);
            check(presenter.accept(state), presenter.diagnostic().c_str());
            presenter.update(.2, 800, 600);
            presenter.render();
            check(Rml::GetContext(0) == context && context->GetFocusElement() == field &&
                      presenter.wants_text() && presenter.presentation_revision() > revision,
                  "Locale change replaced context/focus or retained a stale presentation revision");
            check(text(dom, "title") == "Jouer" &&
                      presenter.localization_state().at("selected_locale") == "fr" &&
                      presenter.localization_state().at("fallback_messages") == 1,
                  "Paused locale switch or default-message fallback did not reach native text");
            auto config = scene.entity("language").get<UiLocalization>();
            config.catalog.id = AssetId::generate();
            scene.entity("language").set(config);
            state = ui->snapshot(scene, "native-localization", 1, 0, true);
            check(!presenter.accept(state) && Rml::GetContext(0) == context &&
                      presenter.localization_state().at("asset") == Json(first) &&
                      text(dom, "title") == "Jouer",
                  "Failed table replacement lost or mixed the complete predecessor");
            config.catalog.id = second;
            scene.entity("language").set(config);
            state = ui->snapshot(scene, "native-localization", 1, 0, true);
            check(presenter.accept(state), presenter.diagnostic().c_str());
            presenter.update(.3, 800, 600);
            auto* replacement = Rml::GetContext(0);
            check(text(replacement->GetDocument(0), "title") == "Continuer" &&
                      presenter.localization_state().at("asset") == Json(second),
                  "Repair did not admit the newly selected catalog");
            auto candidate = state;
            candidate["revision"] = candidate.at("revision").get<std::uint64_t>() + 1;
            candidate["localization"]["asset"] = first;
            candidate["localization"]["locale"] = "en";
            const auto ticket = presenter.prepare(candidate);
            check(presenter.prepared(ticket) && Rml::GetNumContexts() == 2 &&
                      presenter.localization_state().at("selected_locale") == "fr" &&
                      text(replacement->GetDocument(0), "title") == "Continuer",
                  "Staged localization leaked into the active context");
            presenter.cancel_prepared(ticket);
            check(Rml::GetNumContexts() == 1, "Canceled localization context survived");
        }
        check(renderer.geometry.empty() && renderer.textures.empty(),
              "Localized native contexts retained render handles");
        std::filesystem::remove_all(root);
        std::cout << "Native localized text/literal boundaries, paused focus, candidate and "
                     "last-good lifetime passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
