#include "application_state_authoring.hpp"
#include "application_state_gallery_fixture.hpp"
#include "asset_bytes.hpp"
#include "runtime_package.hpp"
#include "ui_test_renderer.hpp"
#include <forge/authoring.hpp>
#include <forge/primitive_catalog.hpp>
#include <forge/render_components.hpp>
#include <iostream>

namespace {
using namespace forge;
using namespace forge::test_state_gallery;
RuntimeUiInspector inspector(const std::filesystem::path& root,
                             const std::vector<std::byte>& font) {
    return [=](AssetId asset, std::stop_token) {
        UiTestRenderer renderer;
        UiPresenter presenter(renderer, root, font);
        return presenter.inspect_static({asset});
    };
}
void prepare(const std::filesystem::path& root, const std::vector<std::byte>& font) {
    const auto directory = root / "Assets/UI/SignalLock";
    std::filesystem::create_directories(directory);
    atomic_write(directory / "hud.rml", R"rml(<rml><head>
<link type="text/rcss" href="hud.rcss"/>
</head><body><div id="panel">
<p class="eyebrow">FORGE / SIGNAL PUZZLE</p><h1>Signal Lock</h1>
<p class="intro">Find the matching signal and route to open the lock.</p>
<div id="menu" data-if="forge_state_app == 'menu'">
<div class="card"><h2>Two choices. One connection.</h2>
<p>The receiver listens for a cool blue signal from the right-hand route.</p>
<p>Choose Play, set both controls, then open the connection.</p></div>
<button id="play" class="primary" data-event-click="command('forge.state', 'app:playing:' + forge_rev_app)">Play</button>
</div>
<div id="game-banner" class="banner" data-if="forge_state_app | state_is('app','game')">Game session active</div>
<div id="game" data-if="forge_state_app == 'playing'">
<div class="card"><p class="caption">1 / SIGNAL</p><h2 id="signal">{{forge_state_signal}}</h2>
<button id="amber" data-class-selected="forge_state_signal == 'amber'" data-event-click="command('forge.state', 'signal:amber:' + forge_rev_signal)">Amber</button>
<button id="cyan" data-class-selected="forge_state_signal == 'cyan'" data-event-click="command('forge.state', 'signal:cyan:' + forge_rev_signal)">Cyan</button>
<button id="violet" data-class-selected="forge_state_signal == 'violet'" data-event-click="command('forge.state', 'signal:violet:' + forge_rev_signal)">Violet</button></div>
<div class="card"><p class="caption">2 / ROUTE</p><h2 id="route">{{forge_state_route}}</h2>
<button id="left" data-class-selected="forge_state_route == 'left'" data-event-click="command('forge.state', 'route:left:' + forge_rev_route)">Left</button>
<button id="right" data-class-selected="forge_state_route == 'right'" data-event-click="command('forge.state', 'route:right:' + forge_rev_route)">Right</button></div>
<div id="closed" class="result" data-if="forge_state_lock == 'closed'"><h2>Connection closed</h2><p>Try another signal or route.</p></div>
<div id="open" class="result success" data-if="forge_state_lock == 'open'"><h2>Connection open</h2>
<p>The receiver recognizes this combination.</p>
<button id="finish" class="primary" data-event-click="command('forge.state', 'app:complete:' + forge_rev_app)">Open connection</button></div>
</div>
<div id="complete" class="card success" data-if="forge_state_app == 'complete'"><h2>Signal received!</h2>
<p>You opened the connection. Return to the menu to reset the puzzle.</p></div>
<button id="menu-button" data-if="forge_state_app != 'menu'" data-event-click="command('forge.state', 'app:menu:' + forge_rev_app)">Return to menu</button>
<div class="rule"></div><p class="caption">STATE MONITOR</p>
<p class="monitor">Mode: {{forge_state_app}} / Lock: {{forge_state_lock}}</p>
<p class="monitor">Paused: {{paused}} / Tick: {{tick}}</p>
<button id="pause" data-event-click="command('Pause')">Pause</button>
<button id="resume" data-event-click="command('Resume')">Resume</button>
<button id="step" data-event-click="command('Step')">Step</button>
<p class="small">Choices still work while paused. Return to menu resets the choices; Stop and Play reset the entire session.</p>
</div></body></rml>)rml");
    atomic_write(directory / "hud.rcss", R"rcss(body,div,p,h1,h2 {display:block;}
body {width:100%;height:100%;font-family:Lato;font-size:16px;color:#e8f0f3;pointer-events:none;}
#panel {position:absolute;left:6%;top:4%;width:88%;max-width:720px;max-height:92%;box-sizing:border-box;padding:22px;background-color:#142634;overflow:auto;pointer-events:auto;}
h1 {font-size:34px;line-height:1.1;margin:8px 0 14px;} h2 {font-size:23px;margin:7px 0 12px;}
p {margin:9px 0;line-height:1.4;} .eyebrow,.caption {font-size:12px;letter-spacing:1px;color:#77d9cc;}
.intro,.small {color:#b7cbd4;} .small,.monitor {font-size:14px;}
.card {background-color:#0c1b26;padding:15px;margin:15px 0;}
button {display:inline-block;box-sizing:border-box;padding:10px 14px;margin:4px 7px 4px 0;background-color:#294658;}
button:hover,button:focus {background-color:#3e677d;} button.selected {background-color:#206e70;border-bottom:3px #80eee0;}
button.primary {background-color:#297975;} button.primary:hover,button.primary:focus {background-color:#37958f;}
.banner {padding:8px 12px;margin:12px 0;color:#8ce8d5;background-color:#183d40;font-size:13px;}
.result {padding:13px;background-color:#3a2d25;margin:14px 0;}
.success {background-color:#143e3d;} .success h2 {color:#8ce8d5;}
.rule {height:1px;background-color:#365060;margin:19px 0 14px;}
scrollbarvertical {width:10px;background-color:#0c1b26;} scrollbarvertical sliderbar {background-color:#416b7a;})rcss");
    const auto lease = std::make_shared<ProjectLease>(root);
    AssetId states;
    if (std::filesystem::exists(root / state_path))
        states = ApplicationStatesDocument(lease, state_path).source().asset();
    else {
        auto document = ApplicationStatesDocument::create(lease, state_path, [](AssetId id) {
            auto source = ApplicationStatesSource::create(id);
            source.document["domains"] = Json::array(
                {{{"key", "app"},
                  {"label", "Game mode"},
                  {"initial", "menu"},
                  {"states",
                   Json::array(
                       {{{"key", "menu"}, {"label", "Menu"}},
                        {{"key", "game"}, {"label", "Game"}},
                        {{"key", "playing"}, {"label", "Playing"}, {"parent", "game"}},
                        {{"key", "complete"}, {"label", "Complete"}, {"parent", "game"}}})}},
                 {{"key", "signal"},
                  {"label", "Signal"},
                  {"initial", "amber"},
                  {"enabled_when", {{"domain", "app"}, {"state", "playing"}}},
                  {"states", Json::array({{{"key", "amber"}, {"label", "Amber"}},
                                          {{"key", "cyan"}, {"label", "Cyan"}},
                                          {{"key", "violet"}, {"label", "Violet"}}})}},
                 {{"key", "route"},
                  {"label", "Route"},
                  {"initial", "left"},
                  {"enabled_when", {{"domain", "app"}, {"state", "playing"}}},
                  {"states", Json::array({{{"key", "left"}, {"label", "Left"}},
                                          {{"key", "right"}, {"label", "Right"}}})}},
                 {{"key", "lock"},
                  {"label", "Connection"},
                  {"computed", true},
                  {"initial", "closed"},
                  {"states", Json::array({{{"key", "closed"}, {"label", "Closed"}},
                                          {{"key", "open"}, {"label", "Open"}}})},
                  {"rules",
                   Json::array(
                       {{{"state", "open"},
                         {"all", Json::array({{{"domain", "app"}, {"state", "playing"}},
                                              {{"domain", "signal"}, {"state", "cyan"}},
                                              {{"domain", "route"}, {"state", "right"}}})}}})}}});
            return source;
        });
        states = document->source().asset();
        document->save();
        AssetImportService imports(lease, application_states_import_registry(),
                                   {"windows", "d3d12", "desktop"}, 1);
        imports.submit(
            imports.prepare(document->locator(), {}, states),
            [](auto& candidate, const auto& plan, const auto&) {
                prepare_application_states_publication(candidate, plan);
            },
            [](const auto&, const auto&) {});
        check(imports.wait_idle(std::chrono::seconds(10)), "Signal Lock publication timed out");
        const auto results = imports.poll();
        check(results.size() == 1 && results.front().published,
              results.empty() ? "Missing state publication" : results.front().diagnostic);
    }
    const auto hud = register_ui_document(root, "Assets/UI/SignalLock/hud.rml");
    WorldContext world;
    Scene scene(world);
    if (std::filesystem::exists(root / scene_path))
        scene.load(root / scene_path);
    else {
        const auto create = [&](const char* name) {
            auto entity = scene.entity(authoring_command(scene, "entity.create", {{"name", name}})
                                           .at("selected")
                                           .get<std::string>());
            entity.set<Primitive>({no_primitive});
            return entity;
        };
        create("Signal Lock UI").set<UiDocument>({{hud.id}, true, true, 0});
        create("Signal Lock - inspect Application States").set<ApplicationStates>({{states}});
        auto camera = create("Game Camera");
        camera.set<Camera>({});
        camera.set<LocalTranslation>({0, 0, -8});
    }
    scene.save(root / scene_path);
    const std::array roots{scene.asset_id()};
    prepare_runtime_content_catalog(*lease, roots, inspector(root, font), scene.schema());
}
void exercise(const std::filesystem::path& root, const std::vector<std::byte>& font,
              const Json& source = Json()) {
    Game game(root, source.is_null() ? read(root / scene_path) : source);
    const auto original = game.scene().snapshot();
    UiTestRenderer renderer;
    UiPresenter presenter(renderer, root, font);
    auto state = game.snapshot();
    presenter.reset(state.at("session"), state.at("generation"));
    check(presenter.accept(state), presenter.diagnostic());
    presenter.update(.1, 800, 720);
    auto context = Rml::GetContext(0);
    auto document = context->GetDocument(0);
    ui_protocol::CommandGate gate;
    gate.reset(state.at("session"), state.at("generation"));
    double now = .1;
    const auto click = [&](const char* id) {
        document->GetElementById(id)->DispatchEvent("click", Rml::Dictionary{});
        check(presenter.pending_command().has_value(), "Signal Lock button did not queue action");
        const auto ack = gate.dispatch(*presenter.pending_command(),
                                       [&](const auto& request) { game.command(request); });
        check(ack.at("ok"), ack.dump());
        presenter.acknowledge(ack);
        check(presenter.accept(game.snapshot()), presenter.diagnostic());
        presenter.update(now += .1, 800, 720);
        check(Rml::GetContext(0) == context, "Signal Lock transition replaced its native context");
    };
    const auto active = [&](const char* domain) {
        return application_state_current(game.scene().world(), domain);
    };
    check(active("app") == "menu" && active("signal").empty(), "Signal Lock did not begin at menu");
    click("play");
    check(active("app") == "playing" && active("signal") == "amber" && active("lock") == "closed",
          "Puzzle initial state is wrong");
    click("right");
    check(active("lock") == "closed", "Wrong signal opened lock");
    click("cyan");
    check(active("lock") == "open", "Matching choices did not open lock");
    click("finish");
    check(active("app") == "complete" && active("signal").empty(),
          "Complete mode retained subordinate choices");
    check(application_state_is(game.scene().world(), "app", "game"),
          "Complete state lost shared Game ancestry");
    click("menu-button");
    click("play");
    check(active("signal") == "amber" && active("route") == "left" && active("lock") == "closed",
          "Return to menu did not reset puzzle");
    check(game.session().status().at("clock").at("tick") == 0 &&
              game.scene().snapshot() == original,
          "Paused state actions altered fixed clock or authored intent");
    click("pause");
    click("step");
    check(game.session().status().at("clock").at("tick") == 1,
          "Gallery Step did not share its GameSession clock");
    presenter.render();
    check(renderer.draws > 0, "Signal Lock native UI did not render geometry");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 4 || argc == 5,
              "Need Gallery, scratch, packaged font and optional --prepare");
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto root = scratch / "Gallery";
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        const auto font = asset_detail::read_bytes(argv[3], 4 * 1024 * 1024);
        if (argc == 5) {
            check(std::string_view(argv[4]) == "--prepare", "Unknown preparation argument");
            prepare(root, font);
            exercise(root, font);
            std::cout << path_utf8(root) << '\n';
            return 0;
        }
        exercise(root, font);
        const auto source = read(root / scene_path);
        const std::array roots{source.at("asset_id").get<AssetId>()};
        const auto package = scratch / "Export";
        package_runtime_content(root, package, roots, {"windows", "d3d12"}, {}, {}, {},
                                inspector(root, font));
        const auto moved = scratch / "Relocated game";
        std::filesystem::rename(package, moved);
        const auto shipped = open_runtime_content(moved, {"windows", "d3d12"});
        check(shipped.records().size() == 4, "Signal Lock scene/state/RML/style closure changed");
        check(!std::filesystem::exists(moved / state_path), "Editable state source was packaged");
        check(read(root / scene_path) == source, "Export rewrote original Scene");
        std::filesystem::rename(root, scratch / "Sources offline");
        exercise(moved, font, read(moved / shipped.records().at(roots[0]).source));
        std::filesystem::remove_all(scratch);
        std::cout << "Signal Lock actual GameSession/native UI, puzzle/reset/paused clock and "
                     "relocated source-free export passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
